// Graphics options: VSync, anisotropic filtering, anti-aliasing.
//
// - VSync: the engine already supports it.  CGfxLibrary::SwapBuffers calls
//   wglSwapIntervalEXT whenever the console variable gfx_fVSyncsToWait
//   changes.  The mod sets that variable every frame from its own setting.
// - Anisotropic filtering: the engine never asks for it.  opengl32.dll's
//   glTexParameteri/f and glBindTexture are hooked (the engine calls them
//   through pointers to those exports; the mod's own import keeps the DLL
//   loaded).  A 2D texture given a mipmapped minification filter is a texture
//   to filter; when it is bound and its anisotropy differs from the setting,
//   GL_TEXTURE_MAX_ANISOTROPY_EXT is set, so a new setting applies at once.
// - Anti-aliasing (MSAA): needs a multisampled pixel format, chosen when the
//   window is made.  Engine.dll's SetPixelFormat import is hooked; the format
//   the engine picked is replaced by one with the same color, depth and
//   stencil bits and the chosen sample count (wglChoosePixelFormatARB, fetched
//   once through a hidden window).  Takes effect when the video mode is set.
//   The world is drawn as screen-space trapezoids from the engine's own
//   scan-line visibility pass.  Engine.dll 0x6002D8D0 fits them to OpenGL by
//   drawing rows top..bottom as y = top + 0.3 .. bottom + 0.6625, which covers
//   every pixel centre but leaves a 0.64 pixel strip between trapezoids.  With
//   multisampling the samples in those strips are never drawn and show old
//   frames as horizontal streaks.  While MSAA is on, each trapezoid is then
//   stretched to exactly top .. bottom + 1 along the edges the engine draws,
//   so the trapezoids tile without gaps or overlaps.
#include "alphafixes.h"

#include <GL/gl.h>
#include <cmath>
#include <unordered_map>

namespace {

// Engine.dll (alpha build)
float *const gfx_fVSyncsToWait = (float *)0x6010D608;

constexpr GLenum GL_TEXTURE_MAX_ANISOTROPY = 0x84FE;
constexpr GLenum GL_MAX_TEXTURE_MAX_ANISOTROPY = 0x84FF;

using TexParameteri_t = void(WINAPI *)(GLenum target, GLenum pname, GLint param);
using TexParameterf_t = void(WINAPI *)(GLenum target, GLenum pname, GLfloat param);
using BindTexture_t = void(WINAPI *)(GLenum target, GLuint texture);
TexParameteri_t _pfnTexParameteri = nullptr;
TexParameterf_t _pfnTexParameterf = nullptr;
BindTexture_t _pfnBindTexture = nullptr;

// per GL context: textures to filter and the anisotropy each one has now
struct ContextTextures {
  GLuint uiBound = 0;
  std::unordered_map<GLuint, float> mapAniso;
};
std::unordered_map<HGLRC, ContextTextures> _mapContexts;
float _fMaxAniso = -1.0f;                    // -1 = not asked yet, 0 = not supported

float WantedAniso()
{
  if (_fMaxAniso < 0.0f) {
    _fMaxAniso = 0.0f;
    const char *strExt = (const char *)glGetString(GL_EXTENSIONS);
    if (strExt != nullptr && strstr(strExt, "GL_EXT_texture_filter_anisotropic") != nullptr) {
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &_fMaxAniso);
    }
    Log("Anisotropic filtering: up to %.0fx", _fMaxAniso);
  }
  if (_fMaxAniso < 1.0f) return 0.0f;
  float f = (float)(g_cfg.iAnisotropy < 1 ? 1 : g_cfg.iAnisotropy);
  return f > _fMaxAniso ? _fMaxAniso : f;
}

void ApplyAniso(ContextTextures &ct, GLuint uiTexture, float fWanted)
{
  auto it = ct.mapAniso.find(uiTexture);
  if (it == ct.mapAniso.end() || it->second == fWanted) return;
  _pfnTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, fWanted);
  it->second = fWanted;
}

void MinFilterSet(GLenum target, GLenum pname, GLint iFilter)
{
  if (target != GL_TEXTURE_2D || pname != GL_TEXTURE_MIN_FILTER) return;
  HGLRC hglrc = wglGetCurrentContext();
  if (hglrc == nullptr || IsOverlayContext(hglrc)) return;
  ContextTextures &ct = _mapContexts[hglrc];
  bool bMipmapped = iFilter >= GL_NEAREST_MIPMAP_NEAREST && iFilter <= GL_LINEAR_MIPMAP_LINEAR;
  if (!bMipmapped) {
    ct.mapAniso.erase(ct.uiBound);
    return;
  }
  float fWanted = WantedAniso();
  if (fWanted == 0.0f) return;
  ct.mapAniso.emplace(ct.uiBound, 1.0f);   // GL's default
  ApplyAniso(ct, ct.uiBound, fWanted);
}

void WINAPI TexParameteri_Hook(GLenum target, GLenum pname, GLint param)
{
  _pfnTexParameteri(target, pname, param);
  MinFilterSet(target, pname, param);
}

void WINAPI TexParameterf_Hook(GLenum target, GLenum pname, GLfloat param)
{
  _pfnTexParameterf(target, pname, param);
  MinFilterSet(target, pname, (GLint)param);
}

void WINAPI BindTexture_Hook(GLenum target, GLuint texture)
{
  _pfnBindTexture(target, texture);
  if (target != GL_TEXTURE_2D) return;
  HGLRC hglrc = wglGetCurrentContext();
  if (hglrc == nullptr || IsOverlayContext(hglrc)) return;
  ContextTextures &ct = _mapContexts[hglrc];
  ct.uiBound = texture;
  if (!ct.mapAniso.empty()) {
    float fWanted = WantedAniso();
    if (fWanted != 0.0f) ApplyAniso(ct, texture, fWanted);
  }
}

// --- anti-aliasing ----------------------------------------------------------------

using ChoosePixelFormatARB_t = BOOL(WINAPI *)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);
using SetPixelFormat_t = BOOL(WINAPI *)(HDC, int, const PIXELFORMATDESCRIPTOR *);
ChoosePixelFormatARB_t _pfnChoosePixelFormatARB = nullptr;
SetPixelFormat_t _pfnSetPixelFormat = SetPixelFormat;
bool _bTriedARB = false;
bool _bMultisampled = false;                 // the game's window has a multisampled pixel format

// Engine.dll (alpha build): fits the world's trapezoids to OpenGL's pixel centres
constexpr uintptr_t ENG_FIT_TRAPEZOIDS = 0x6002D8D0;     // void (polygon list)
// polygon: +0 next, +4 first trapezoid.  trapezoid: +0 next, x +4 (top left, top right,
// bottom right, bottom left), y +0x14 (same order), area +0x64
using FitTrapezoids_t = void(__cdecl *)(char *pPolygons);
FitTrapezoids_t _pfnFitTrapezoids = nullptr;

// The engine's own fitting gives rows top..bottom the y range top + 0.3 ..
// bottom + 0.6625.  With MSAA the range becomes exactly top .. bottom + 1, each
// side extended along the line the engine draws, so every row keeps the left
// and right ends it has at its pixel centre without MSAA.
void __cdecl FitTrapezoids_Hook(char *pPolygons)
{
  _pfnFitTrapezoids(pPolygons);
  if (!_bMultisampled) return;
  for (char *pPolygon = pPolygons; pPolygon != nullptr; pPolygon = *(char **)pPolygon) {
    for (char *pt = *(char **)(pPolygon + 4); pt != nullptr; pt = *(char **)pt) {
      float *pfX = (float *)(pt + 4), *pfY = (float *)(pt + 0x14);
      float fTop = pfY[0], fBottom = pfY[3];
      float fNewTop = floorf(fTop), fNewBottom = floorf(fBottom) + 1.0f;
      float fHeight = fBottom - fTop;
      if (fHeight > 0.0f) {
        float fUp = (fTop - fNewTop) / fHeight, fDown = (fNewBottom - fBottom) / fHeight;
        float fLeftSlope = pfX[3] - pfX[0], fRightSlope = pfX[2] - pfX[1];
        float x0 = pfX[0] - fLeftSlope * fUp, x3 = pfX[3] + fLeftSlope * fDown;
        float x1 = pfX[1] - fRightSlope * fUp, x2 = pfX[2] + fRightSlope * fDown;
        if (x0 <= x1 && x3 <= x2) {   // sides that would cross keep their x
          pfX[0] = x0; pfX[1] = x1; pfX[2] = x2; pfX[3] = x3;
        }
      }
      pfY[0] = pfY[1] = fNewTop;
      pfY[2] = pfY[3] = fNewBottom;
      *(float *)(pt + 0x64) = 0.5f * ((pfX[1] - pfX[0]) + (pfX[2] - pfX[3])) * (fNewBottom - fNewTop);
    }
  }
}

// wglChoosePixelFormatARB is only reachable through a current context.
void FetchChoosePixelFormatARB()
{
  _bTriedARB = true;
  WNDCLASSA wc = {};
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = "AlphaFixes_PixelFormat";
  RegisterClassA(&wc);
  HWND hwnd = CreateWindowA(wc.lpszClassName, "", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, wc.hInstance, nullptr);
  HDC hdc = GetDC(hwnd);
  PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 };
  HGLRC hglrcOld = wglGetCurrentContext();
  HDC hdcOld = wglGetCurrentDC();
  if (_pfnSetPixelFormat(hdc, ChoosePixelFormat(hdc, &pfd), &pfd)) {
    if (HGLRC hglrc = wglCreateContext(hdc)) {
      wglMakeCurrent(hdc, hglrc);
      _pfnChoosePixelFormatARB = (ChoosePixelFormatARB_t)wglGetProcAddress("wglChoosePixelFormatARB");
      wglMakeCurrent(hdcOld, hglrcOld);
      wglDeleteContext(hglrc);
    }
  }
  ReleaseDC(hwnd, hdc);
  DestroyWindow(hwnd);
  if (_pfnChoosePixelFormatARB == nullptr) Log("Anti-aliasing: wglChoosePixelFormatARB not available");
}

BOOL WINAPI SetPixelFormat_Hook(HDC hdc, int iFormat, const PIXELFORMATDESCRIPTOR *ppfd)
{
  int ctSamples = g_cfg.iAntiAliasing;
  if (ctSamples >= 2 && ppfd != nullptr) {
    if (!_bTriedARB) FetchChoosePixelFormatARB();
    PIXELFORMATDESCRIPTOR pfd = {};
    DescribePixelFormat(hdc, iFormat, sizeof(pfd), &pfd);   // what the engine got
    while (_pfnChoosePixelFormatARB != nullptr && ctSamples >= 2) {
      const int aiAttribs[] = {
        0x2001 /* WGL_DRAW_TO_WINDOW_ARB */, 1, 0x2010 /* WGL_SUPPORT_OPENGL_ARB */, 1,
        0x2011 /* WGL_DOUBLE_BUFFER_ARB */, 1, 0x2013 /* WGL_PIXEL_TYPE_ARB */, 0x202B /* WGL_TYPE_RGBA_ARB */,
        0x2003 /* WGL_ACCELERATION_ARB */, 0x2027 /* WGL_FULL_ACCELERATION_ARB */,
        0x2014 /* WGL_COLOR_BITS_ARB */, pfd.cColorBits, 0x2022 /* WGL_DEPTH_BITS_ARB */, pfd.cDepthBits,
        0x2023 /* WGL_STENCIL_BITS_ARB */, pfd.cStencilBits,
        0x2041 /* WGL_SAMPLE_BUFFERS_ARB */, 1, 0x2042 /* WGL_SAMPLES_ARB */, ctSamples, 0 };
      int iMsaa = 0;
      UINT ctFound = 0;
      if (_pfnChoosePixelFormatARB(hdc, aiAttribs, nullptr, 1, &iMsaa, &ctFound) && ctFound > 0) {
        PIXELFORMATDESCRIPTOR pfdMsaa = {};
        DescribePixelFormat(hdc, iMsaa, sizeof(pfdMsaa), &pfdMsaa);
        if (_pfnSetPixelFormat(hdc, iMsaa, &pfdMsaa)) {
          Log("Anti-aliasing: %dx MSAA (pixel format %d instead of %d)", ctSamples, iMsaa, iFormat);
          _bMultisampled = true;
          return TRUE;
        }
      }
      ctSamples /= 2;   // try fewer samples
    }
    Log("Anti-aliasing: no multisampled pixel format, using the engine's");
  }
  _bMultisampled = false;
  return _pfnSetPixelFormat(hdc, iFormat, ppfd);
}

}  // namespace

void InstallGraphicsOptions()
{
  HMODULE hGL = GetModuleHandleA("opengl32.dll");
  bool bOk = HookFunction((void *)GetProcAddress(hGL, "glTexParameteri"), (void *)TexParameteri_Hook,
                          (void **)&_pfnTexParameteri, "glTexParameteri");
  bOk &= HookFunction((void *)GetProcAddress(hGL, "glTexParameterf"), (void *)TexParameterf_Hook,
                      (void **)&_pfnTexParameterf, "glTexParameterf");
  bOk &= HookFunction((void *)GetProcAddress(hGL, "glBindTexture"), (void *)BindTexture_Hook,
                      (void **)&_pfnBindTexture, "glBindTexture");
  if (void *p = HookImport(GetModuleHandleA("Engine.dll"), "GDI32.dll", "SetPixelFormat", (void *)SetPixelFormat_Hook)) {
    _pfnSetPixelFormat = (SetPixelFormat_t)p;
  } else {
    bOk = false;
  }
  // push ebp; mov ebp, esp; mov ecx, [ebp + 8]; test ecx, ecx; je
  static const BYTE aubFit[] = { 0x55, 0x8B, 0xEC, 0x8B, 0x4D, 0x08, 0x85, 0xC9, 0x0F, 0x84 };
  bOk &= CodeMatches(ENG_FIT_TRAPEZOIDS, aubFit, sizeof(aubFit))
      && HookFunction((void *)ENG_FIT_TRAPEZOIDS, (void *)FitTrapezoids_Hook, (void **)&_pfnFitTrapezoids, "trapezoid fitting");
  Log("Graphics options: %s", bOk ? "installed" : "partly failed");
}

// Every frame, from the SwapBuffers hook; the engine applies VSync on its next frame.
void UpdateGraphicsOptions()
{
  *gfx_fVSyncsToWait = g_cfg.bVSync ? 1.0f : 0.0f;
  UpdateCrosshair();
}

// The engine deleted its GL context: texture names start over in the next one.
void ForgetGLContext()
{
  _mapContexts.clear();
}
