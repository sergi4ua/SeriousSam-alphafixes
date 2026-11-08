// OpenGL diagnostics (test builds).
//  - AF_LOG_GLSTATE=1: every draw's render state (enabled caps, blending, alpha test, depth, texture
//    environment, generation and matrix per texture unit, vertex array formats, bound texture formats) is
//    reduced to a signature; the first draw with each signature (glDrawElements, glDrawArrays, glBegin) is
//    logged.  Also new records in draw size, and draws over 20 ms.
//  - AF_LOG_GLTIME=1: every call through Engine.dll's OpenGL table is timed (calls over 8 ms logged); on a
//    long frame, the calls the frame before didn't make are listed; texture uploads are counted, and
//    textures redefined with a new size or format are logged.
#include "alphafixes.h"

#if AF_TEST_AIDS

#include <unordered_set>
#include <map>

namespace {

// Engine.dll's table of OpenGL function pointers (filled by CGfxLibrary::CreateContext)
constexpr uintptr_t GL_ENABLE = 0x6010CF30, GL_DISABLE = 0x6010CF34;
constexpr uintptr_t GL_ENABLECLIENT = 0x6010CF3C, GL_DISABLECLIENT = 0x6010CF40;
constexpr uintptr_t GL_ALPHAFUNC = 0x6010CEE4, GL_BLENDFUNC = 0x6010CEE8, GL_DEPTHFUNC = 0x6010CF80, GL_DEPTHMASK = 0x6010CF84;
constexpr uintptr_t GL_SHADEMODEL = 0x6010D25C, GL_TEXENVI = 0x6010D318, GL_TEXENVF = 0x6010D314, GL_TEXGENI = 0x6010D2F8;
constexpr uintptr_t GL_VERTEXPTR = 0x6010D230, GL_COLORPTR = 0x6010D238, GL_TEXCOORDPTR = 0x6010D240;
constexpr uintptr_t GL_DRAWELEMENTS = 0x6010D254, GL_DRAWARRAYS = 0x6010D250, GL_BEGIN = 0x6010CFF8;
constexpr uintptr_t GL_BINDTEXTURE = 0x6010D360, GL_TEXIMAGE2D = 0x6010D350;
constexpr uintptr_t GL_SELECTTEXSGIS = 0x6010D4FC, GL_SELECTTEXEXT = 0x6010D588;
constexpr uintptr_t GL_FOGI = 0x6010D3E8, GL_POLYGONOFFSET = 0x6010CF08, GL_COLORMASK = 0x6010CEE0;
constexpr uintptr_t GL_MATRIXMODE = 0x6010CF94, GL_LOADIDENTITY = 0x6010CFAC, GL_LOADMATRIXF = 0x6010CFB4, GL_MULTMATRIXF = 0x6010CFBC;
constexpr uintptr_t GL_TRANSLATEF = 0x6010CFD4, GL_SCALEF = 0x6010CFCC, GL_ROTATEF = 0x6010CFC4;
constexpr uintptr_t GL_TEXGENFV = 0x6010D300, GL_FOGF = 0x6010D3E4, GL_FOGFV = 0x6010D3EC, GL_LIGHTMODELI = 0x6010D27C;

struct State {
  unsigned char aubCaps[64];       // enabled caps (hashed into slots), per texture unit for texturing caps
  unsigned aulBlend[2], ulAlphaFunc, ulDepthFunc, ulDepthMask, ulShade, ulFog, ulColorMask;
  unsigned aulTexEnv[4], aulTexGen[4][2], aulTexFormat[4];
  unsigned ulClient, aulVertexFmt[2], aulColorFmt[2], aulTexCoordFmt[4][2];
  unsigned ulMode;
  unsigned aulTexMatrix[4];        // the texture matrix isn't identity
};
unsigned _ulMatrixMode = 0x1700;
State _st = {};
int _iUnit = 0;
std::map<unsigned, unsigned> _mapTexFormat;      // texture name -> internal format
unsigned _aulBound[4] = {};
std::unordered_set<unsigned long long> _setSeen;
double _dFreq = 1.0;
double Now() { LARGE_INTEGER ll; QueryPerformanceCounter(&ll); return ll.QuadPart * 1000.0 / _dFreq; }
bool _bOn = false;
int _ctNew = 0;

std::map<uintptr_t, void *> _mapOriginal;
template <class T> T Orig(uintptr_t a) { return (T)_mapOriginal[a]; }

int CapSlot(unsigned cap)
{
  switch (cap) {
  case 0x0DE1: return 0 + _iUnit;   // GL_TEXTURE_2D, per unit
  case 0x0C60: return 4 + _iUnit;   // GL_TEXTURE_GEN_S
  case 0x0C61: return 8 + _iUnit;   // GL_TEXTURE_GEN_T
  default: return 12 + (int)(cap * 2654435761u >> 26) % 52;
  }
}

unsigned long long Hash()
{
  _st.aulTexFormat[0] = _mapTexFormat[_aulBound[0]];
  for (int i = 1; i < 4; i++) _st.aulTexFormat[i] = _st.aubCaps[i] ? _mapTexFormat[_aulBound[i]] : 0;
  unsigned long long h = 1469598103934665603ull;
  const unsigned char *pub = (const unsigned char *)&_st;
  for (size_t i = 0; i < sizeof(_st); i++) { h ^= pub[i]; h *= 1099511628211ull; }
  return h;
}

void Describe(char *str, size_t ct)
{
  int n = sprintf_s(str, ct, "mode %u caps", _st.ulMode);
  for (int i = 0; i < 64 && n < (int)ct - 8; i++) if (_st.aubCaps[i]) n += sprintf_s(str + n, ct - n, " %d", i);
  n += sprintf_s(str + n, ct - n, " blend %X/%X alpha %X depth %X/%u shade %X fog %X cmask %X client %X vtx %u/%X col %u/%X",
                 _st.aulBlend[0], _st.aulBlend[1], _st.ulAlphaFunc, _st.ulDepthFunc, _st.ulDepthMask, _st.ulShade, _st.ulFog,
                 _st.ulColorMask, _st.ulClient, _st.aulVertexFmt[0], _st.aulVertexFmt[1], _st.aulColorFmt[0], _st.aulColorFmt[1]);
  for (int u = 0; u < 2 && n < (int)ct - 40; u++) {
    n += sprintf_s(str + n, ct - n, " | unit%d env %X gen %X/%X fmt %X tc %u/%X tm %u", u, _st.aulTexEnv[u], _st.aulTexGen[u][0],
                   _st.aulTexGen[u][1], _st.aulTexFormat[u], _st.aulTexCoordFmt[u][0], _st.aulTexCoordFmt[u][1], _st.aulTexMatrix[u]);
  }
}

void BeforeDraw(unsigned mode, double &tStart)
{
  _st.ulMode = mode;
  tStart = Now();
}

void AfterDraw(double tStart)
{
  unsigned long long h = Hash();
  if (_setSeen.insert(h).second) {
    double dt = Now() - tStart;
    char str[700];
    Describe(str, sizeof(str));
    Log("GLSTATE new #%d at %.0f, draw %.2f ms: %s", ++_ctNew, tStart, dt, str);
  }
}

void WINAPI Enable_W(unsigned cap) { _st.aubCaps[CapSlot(cap)] = 1; Orig<void(WINAPI *)(unsigned)>(GL_ENABLE)(cap); }
void WINAPI Disable_W(unsigned cap) { _st.aubCaps[CapSlot(cap)] = 0; Orig<void(WINAPI *)(unsigned)>(GL_DISABLE)(cap); }
void WINAPI EnableClient_W(unsigned a) { _st.ulClient |= 1u << (a & 15); Orig<void(WINAPI *)(unsigned)>(GL_ENABLECLIENT)(a); }
void WINAPI DisableClient_W(unsigned a) { _st.ulClient &= ~(1u << (a & 15)); Orig<void(WINAPI *)(unsigned)>(GL_DISABLECLIENT)(a); }
void WINAPI AlphaFunc_W(unsigned f, float r) { _st.ulAlphaFunc = f; Orig<void(WINAPI *)(unsigned, float)>(GL_ALPHAFUNC)(f, r); }
void WINAPI BlendFunc_W(unsigned s, unsigned d) { _st.aulBlend[0] = s; _st.aulBlend[1] = d; Orig<void(WINAPI *)(unsigned, unsigned)>(GL_BLENDFUNC)(s, d); }
void WINAPI DepthFunc_W(unsigned f) { _st.ulDepthFunc = f; Orig<void(WINAPI *)(unsigned)>(GL_DEPTHFUNC)(f); }
void WINAPI DepthMask_W(unsigned char m) { _st.ulDepthMask = m; Orig<void(WINAPI *)(unsigned char)>(GL_DEPTHMASK)(m); }
void WINAPI ShadeModel_W(unsigned m) { _st.ulShade = m; Orig<void(WINAPI *)(unsigned)>(GL_SHADEMODEL)(m); }
void WINAPI Fogi_W(unsigned p, int v) { if (p == 0x0B65) _st.ulFog = v; Orig<void(WINAPI *)(unsigned, int)>(GL_FOGI)(p, v); }
void WINAPI ColorMask_W(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  _st.ulColorMask = (r ? 1 : 0) | (g ? 2 : 0) | (b ? 4 : 0) | (a ? 8 : 0);
  Orig<void(WINAPI *)(unsigned char, unsigned char, unsigned char, unsigned char)>(GL_COLORMASK)(r, g, b, a);
}
void WINAPI TexEnvi_W(unsigned t, unsigned p, int v) { if (p == 0x2200) _st.aulTexEnv[_iUnit & 3] = v; Orig<void(WINAPI *)(unsigned, unsigned, int)>(GL_TEXENVI)(t, p, v); }
void WINAPI TexEnvf_W(unsigned t, unsigned p, float v) { if (p == 0x2200) _st.aulTexEnv[_iUnit & 3] = (unsigned)v; Orig<void(WINAPI *)(unsigned, unsigned, float)>(GL_TEXENVF)(t, p, v); }
void WINAPI TexGeni_W(unsigned c, unsigned p, int v)
{
  if (p == 0x2500 && (c == 0x2000 || c == 0x2001)) _st.aulTexGen[_iUnit & 3][c - 0x2000] = v;
  Orig<void(WINAPI *)(unsigned, unsigned, int)>(GL_TEXGENI)(c, p, v);
}
void WINAPI VertexPtr_W(int s, unsigned t, int st, const void *p) { _st.aulVertexFmt[0] = s; _st.aulVertexFmt[1] = t; Orig<void(WINAPI *)(int, unsigned, int, const void *)>(GL_VERTEXPTR)(s, t, st, p); }
void WINAPI ColorPtr_W(int s, unsigned t, int st, const void *p) { _st.aulColorFmt[0] = s; _st.aulColorFmt[1] = t; Orig<void(WINAPI *)(int, unsigned, int, const void *)>(GL_COLORPTR)(s, t, st, p); }
void WINAPI TexCoordPtr_W(int s, unsigned t, int st, const void *p)
{
  _st.aulTexCoordFmt[_iUnit & 3][0] = s; _st.aulTexCoordFmt[_iUnit & 3][1] = t;
  Orig<void(WINAPI *)(int, unsigned, int, const void *)>(GL_TEXCOORDPTR)(s, t, st, p);
}
void WINAPI BindTexture_W(unsigned t, unsigned name) { if (t == 0x0DE1) _aulBound[_iUnit & 3] = name; Orig<void(WINAPI *)(unsigned, unsigned)>(GL_BINDTEXTURE)(t, name); }
void WINAPI TexImage2D_W(unsigned t, int l, int ifmt, int w, int h, int b, unsigned f, unsigned ty, const void *p)
{
  if (l == 0) _mapTexFormat[_aulBound[_iUnit & 3]] = (unsigned)ifmt;
  Orig<void(WINAPI *)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *)>(GL_TEXIMAGE2D)(t, l, ifmt, w, h, b, f, ty, p);
}
void WINAPI MatrixMode_W(unsigned m) { _ulMatrixMode = m; Orig<void(WINAPI *)(unsigned)>(GL_MATRIXMODE)(m); }
void TexMatrix(bool bIdentity) { if (_ulMatrixMode == 0x1702) _st.aulTexMatrix[_iUnit & 3] = bIdentity ? 0 : 1; }
void WINAPI LoadIdentity_W() { TexMatrix(true); Orig<void(WINAPI *)()>(GL_LOADIDENTITY)(); }
void WINAPI LoadMatrixf_W(const float *pf)
{
  bool bId = true;
  for (int i = 0; i < 16; i++) if (pf[i] != ((i % 5 == 0) ? 1.0f : 0.0f)) bId = false;
  TexMatrix(bId);
  Orig<void(WINAPI *)(const float *)>(GL_LOADMATRIXF)(pf);
}
void WINAPI MultMatrixf_W(const float *pf) { TexMatrix(false); Orig<void(WINAPI *)(const float *)>(GL_MULTMATRIXF)(pf); }
void WINAPI Translatef_W(float x, float y, float z) { TexMatrix(false); Orig<void(WINAPI *)(float, float, float)>(GL_TRANSLATEF)(x, y, z); }
void WINAPI Scalef_W(float x, float y, float z) { TexMatrix(false); Orig<void(WINAPI *)(float, float, float)>(GL_SCALEF)(x, y, z); }
void WINAPI Rotatef_W(float a, float x, float y, float z) { TexMatrix(false); Orig<void(WINAPI *)(float, float, float, float)>(GL_ROTATEF)(a, x, y, z); }
void WINAPI SelectTexSGIS_W(unsigned u) { _iUnit = (int)(u & 3); Orig<void(WINAPI *)(unsigned)>(GL_SELECTTEXSGIS)(u); }
void WINAPI SelectTexEXT_W(unsigned u) { _iUnit = (int)(u & 3); Orig<void(WINAPI *)(unsigned)>(GL_SELECTTEXEXT)(u); }
int _ctMaxIndices = 0;
unsigned _ulMaxIndex = 0;
void WINAPI DrawElements_W(unsigned m, int c, unsigned t, const void *i)
{
  double t0; BeforeDraw(m, t0);
  // the highest vertex index the draw uses: how much of the client arrays the driver has to copy
  unsigned ulMax = 0;
  if (t == 0x1405 && i != nullptr) for (int k = 0; k < c; k++) if (((const unsigned *)i)[k] > ulMax) ulMax = ((const unsigned *)i)[k];
  Orig<void(WINAPI *)(unsigned, int, unsigned, const void *)>(GL_DRAWELEMENTS)(m, c, t, i);
  double dt = Now() - t0;
  if (c > _ctMaxIndices || ulMax > _ulMaxIndex || dt > 20.0) {
    Log("GLSTATE draw at %.0f, %.2f ms: %d indices (record %d), vertices up to %u (record %u)", t0, dt, c, _ctMaxIndices,
        ulMax, _ulMaxIndex);
    if (c > _ctMaxIndices) _ctMaxIndices = c;
    if (ulMax > _ulMaxIndex) _ulMaxIndex = ulMax;
  }
  AfterDraw(t0);
}
void WINAPI DrawArrays_W(unsigned m, int f, int c)
{
  double t0; BeforeDraw(m, t0);
  Orig<void(WINAPI *)(unsigned, int, int)>(GL_DRAWARRAYS)(m, f, c);
  AfterDraw(t0);
}

// immediate mode: the state at glBegin
void WINAPI Begin_W(unsigned m)
{
  _st.ulMode = 0x100 | m;
  unsigned long long h = Hash();
  if (_setSeen.insert(h).second) {
    char str[700];
    Describe(str, sizeof(str));
    Log("GLSTATE new #%d at %.0f (glBegin): %s", ++_ctNew, Now(), str);
  }
  Orig<void(WINAPI *)(unsigned)>(GL_BEGIN)(m);
}

const struct { uintptr_t a; void *pv; } _aWrap[] = {
  { GL_BEGIN, (void *)Begin_W },
  { GL_ENABLE, (void *)Enable_W }, { GL_DISABLE, (void *)Disable_W }, { GL_ENABLECLIENT, (void *)EnableClient_W },
  { GL_DISABLECLIENT, (void *)DisableClient_W }, { GL_ALPHAFUNC, (void *)AlphaFunc_W }, { GL_BLENDFUNC, (void *)BlendFunc_W },
  { GL_DEPTHFUNC, (void *)DepthFunc_W }, { GL_DEPTHMASK, (void *)DepthMask_W }, { GL_SHADEMODEL, (void *)ShadeModel_W },
  { GL_FOGI, (void *)Fogi_W }, { GL_COLORMASK, (void *)ColorMask_W }, { GL_TEXENVI, (void *)TexEnvi_W },
  { GL_TEXENVF, (void *)TexEnvf_W }, { GL_TEXGENI, (void *)TexGeni_W }, { GL_VERTEXPTR, (void *)VertexPtr_W },
  { GL_COLORPTR, (void *)ColorPtr_W }, { GL_TEXCOORDPTR, (void *)TexCoordPtr_W }, { GL_BINDTEXTURE, (void *)BindTexture_W },
  { GL_TEXIMAGE2D, (void *)TexImage2D_W }, { GL_SELECTTEXSGIS, (void *)SelectTexSGIS_W },
  { GL_SELECTTEXEXT, (void *)SelectTexEXT_W }, { GL_DRAWELEMENTS, (void *)DrawElements_W }, { GL_DRAWARRAYS, (void *)DrawArrays_W },
  { GL_MATRIXMODE, (void *)MatrixMode_W }, { GL_LOADIDENTITY, (void *)LoadIdentity_W }, { GL_LOADMATRIXF, (void *)LoadMatrixf_W },
  { GL_MULTMATRIXF, (void *)MultMatrixf_W }, { GL_TRANSLATEF, (void *)Translatef_W }, { GL_SCALEF, (void *)Scalef_W },
  { GL_ROTATEF, (void *)Rotatef_W },
};

// --- timing of every OpenGL call (AF_LOG_GLTIME=1) ----------------------------------------
// Each table entry gets a thunk "push i; jmp Enter".  Enter swaps the call's return address for Leave
// (kept on a shadow stack with the start time) and goes on to the real function, which returns to Leave.

const struct { uintptr_t a; const char *str; } _aGlTable[] = {
#include "gltable.inc"
};
constexpr int CT_GL = sizeof(_aGlTable) / sizeof(_aGlTable[0]);
void *_apvGlOriginal[CT_GL];
BYTE *_pubThunks = nullptr;
bool _bTime = false;
struct Frame { void *pvRet; int i; LONGLONG ll; };
Frame _afStack[64];
int _iDepth = 0;
LONGLONG _llFreq = 1;

void GlLeaveThunk();

// texture uploads this frame (glTexImage2D, glTexSubImage2D): count, pixels, time
int _ctUploads = 0;
long long _llUploadPixels = 0;
double _dUploadMs = 0.0;
int _iTexImage2D = -1, _iTexSubImage2D = -1;

// this frame's and the last frame's calls per function, and this frame's calls with their first arguments
int _actCalls[2][600];
int _iCur = 0;
struct Call { short i; int a0, a1, a2, a3; };
Call _aCalls[20000];
int _ctCalls = 0;

// texture names: what level 0 was last defined as, and the bound name
struct TexDef { int w, h, ifmt; };
std::map<unsigned, TexDef> _mapTexDef;
unsigned _ulBoundName = 0;
int _iBindTexture = -1, _iDeleteTextures = -1;
int _iVertexPtr = -1, _iColorPtr = -1, _iTexCoordPtr = -1, _iNormalPtr = -1;
std::unordered_set<unsigned> _setPointers;
int _ctRedefined = 0;

extern "C" void *__cdecl GlEnter(void **ppvRet, int i)
{
  _actCalls[_iCur][i]++;
  if (i == _iVertexPtr || i == _iColorPtr || i == _iTexCoordPtr || i == _iNormalPtr) {
    const unsigned *pul = (const unsigned *)(ppvRet + 1);
    unsigned ulPtr = i == _iNormalPtr ? pul[2] : pul[3];
    if (ulPtr != 0 && _setPointers.insert(ulPtr).second) {
      LARGE_INTEGER ll;
      QueryPerformanceCounter(&ll);
      Log("GLTIME new array pointer %08X for %s at %.0f", ulPtr, _aGlTable[i].str, ll.QuadPart * 1000.0 / _llFreq);
    }
  }
  if (i == _iBindTexture) {
    const int *pi = (const int *)(ppvRet + 1);
    if (pi[0] == 0x0DE1) _ulBoundName = (unsigned)pi[1];
  } else if (i == _iTexImage2D) {
    const int *pi = (const int *)(ppvRet + 1);
    if (pi[1] == 0) {
      TexDef td = { pi[3], pi[4], pi[2] };
      auto it = _mapTexDef.find(_ulBoundName);
      if (it != _mapTexDef.end() && (it->second.w != td.w || it->second.h != td.h || it->second.ifmt != td.ifmt)) {
        if (_ctRedefined++ < 40) {
          Log("GLTIME texture %u redefined: %dx%d %X -> %dx%d %X", _ulBoundName, it->second.w, it->second.h, it->second.ifmt,
              td.w, td.h, td.ifmt);
        }
      }
      _mapTexDef[_ulBoundName] = td;
    }
  }
  if (_ctCalls < 20000) {
    const int *pi = (const int *)(ppvRet + 1);
    _aCalls[_ctCalls++] = { (short)i, pi[0], pi[1], pi[2], pi[3] };
  }
  if (i == _iTexImage2D || i == _iTexSubImage2D) {
    const int *pi = (const int *)(ppvRet + 1);   // the call's arguments
    _llUploadPixels += i == _iTexImage2D ? (long long)pi[3] * pi[4] : (long long)pi[4] * pi[5];
    _ctUploads++;
  }
  if (_iDepth < 64) {
    LARGE_INTEGER ll;
    QueryPerformanceCounter(&ll);
    _afStack[_iDepth++] = { *ppvRet, i, ll.QuadPart };
    *ppvRet = (void *)GlLeaveThunk;
  }
  return _apvGlOriginal[i];
}

extern "C" void *__cdecl GlLeave()
{
  LARGE_INTEGER ll;
  QueryPerformanceCounter(&ll);
  Frame f = _afStack[--_iDepth];
  double dt = (ll.QuadPart - f.ll) * 1000.0 / _llFreq;
  if (f.i == _iTexImage2D || f.i == _iTexSubImage2D) _dUploadMs += dt;
  if (dt > 8.0) Log("GLTIME %s took %.1f ms at %.0f", _aGlTable[f.i].str, dt, f.ll * 1000.0 / _llFreq);
  return f.pvRet;
}

__declspec(naked) void GlEnterThunk()
{
  __asm {
    pushad
    pushfd
    push  dword ptr [esp + 36]     // i
    lea   eax, [esp + 44]          // the return address slot
    push  eax
    call  GlEnter
    add   esp, 8
    mov   [esp + 36], eax          // i's slot now holds the real function
    popfd
    popad
    ret                            // to the real function, which returns to GlLeaveThunk
  }
}

__declspec(naked) void GlLeaveThunk()
{
  __asm {
    push  eax                      // slot for the caller's return address
    pushad
    pushfd
    call  GlLeave
    mov   [esp + 36], eax
    popfd
    popad                          // the function's own eax back
    ret
  }
}

void UpdateGlTiming()
{
  if (_pubThunks == nullptr) {
    for (int i = 0; i < CT_GL; i++) {
      if (strcmp(_aGlTable[i].str, "glTexImage2D") == 0) _iTexImage2D = i;
      if (strcmp(_aGlTable[i].str, "glTexSubImage2D") == 0) _iTexSubImage2D = i;
      if (strcmp(_aGlTable[i].str, "glBindTexture") == 0) _iBindTexture = i;
      if (strcmp(_aGlTable[i].str, "glDeleteTextures") == 0) _iDeleteTextures = i;
      if (strcmp(_aGlTable[i].str, "glVertexPointer") == 0) _iVertexPtr = i;
      if (strcmp(_aGlTable[i].str, "glColorPointer") == 0) _iColorPtr = i;
      if (strcmp(_aGlTable[i].str, "glTexCoordPointer") == 0) _iTexCoordPtr = i;
      if (strcmp(_aGlTable[i].str, "glNormalPointer") == 0) _iNormalPtr = i;
    }
    _pubThunks = (BYTE *)VirtualAlloc(nullptr, CT_GL * 10, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    for (int i = 0; i < CT_GL; i++) {
      BYTE *pub = _pubThunks + i * 10;
      pub[0] = 0x68; *(int *)(pub + 1) = i;
      pub[5] = 0xE9; *(int *)(pub + 6) = (int)((BYTE *)GlEnterThunk - (pub + 10));
    }
  }
  for (int i = 0; i < CT_GL; i++) {
    void **ppv = (void **)_aGlTable[i].a;
    void *pvThunk = _pubThunks + i * 10;
    if (*ppv != nullptr && *ppv != pvThunk) {
      _apvGlOriginal[i] = *ppv;
      *ppv = pvThunk;
    }
  }
}

}  // namespace

// at the end of a frame: on a long frame, the calls to functions the frame before didn't call
void EndGlFrame(bool bLong)
{
  if (!_bTime) return;
  if (bLong) {
    int iPrev = 1 - _iCur, ct = 0;
    for (int k = 0; k < _ctCalls && ct < 60; k++) {
      int i = _aCalls[k].i;
      if (_actCalls[iPrev][i] == 0) {
        Log("GLTIME   new in this frame: %s(%X, %X, %X, %X) [call %d of %d]", _aGlTable[i].str, _aCalls[k].a0, _aCalls[k].a1,
            _aCalls[k].a2, _aCalls[k].a3, k, _ctCalls);
        if (ct == 0) {
          for (int j = k - 12 > 0 ? k - 12 : 0; j < k + 30 && j < _ctCalls; j++) {
            Log("GLTIME     #%d %s(%X, %X, %X, %X)", j, _aGlTable[_aCalls[j].i].str, _aCalls[j].a0, _aCalls[j].a1, _aCalls[j].a2, _aCalls[j].a3);
          }
        }
        ct++;
      }
    }
    for (int i = 0; i < CT_GL; i++) {
      int d = _actCalls[_iCur][i] - _actCalls[iPrev][i];
      if (d > 20 || d < -20) Log("GLTIME   %s: %d calls (frame before %d)", _aGlTable[i].str, _actCalls[_iCur][i], _actCalls[iPrev][i]);
    }
  }
  _iCur = 1 - _iCur;
  memset(_actCalls[_iCur], 0, sizeof(_actCalls[_iCur]));
  _ctCalls = 0;
}

// texture uploads since the last call (for perfdiag's long frame lines)
void TakeGlUploadStats(int &ct, long long &llPixels, double &dMs)
{
  ct = _ctUploads; llPixels = _llUploadPixels; dMs = _dUploadMs;
  _ctUploads = 0; _llUploadPixels = 0; _dUploadMs = 0.0;
}

// every frame (SwapBuffers): wrap the table's entries again after the engine (re)filled it
void UpdateGlStateDiag()
{
  if (_bTime) { UpdateGlTiming(); return; }
  if (!_bOn) return;
  for (const auto &w : _aWrap) {
    void **ppv = (void **)w.a;
    if (*ppv != nullptr && *ppv != w.pv) {
      _mapOriginal[w.a] = *ppv;
      *ppv = w.pv;
    }
  }
}

void InstallGlStateDiag()
{
  char str[8];
  _bOn = GetEnvironmentVariableA("AF_LOG_GLSTATE", str, sizeof(str)) > 0 && str[0] == '1';
  _bTime = GetEnvironmentVariableA("AF_LOG_GLTIME", str, sizeof(str)) > 0 && str[0] == '1';
  LARGE_INTEGER ll;
  QueryPerformanceFrequency(&ll);
  _dFreq = (double)ll.QuadPart;
  _llFreq = ll.QuadPart;
  if (_bTime) { _bOn = false; Log("GLTIME diagnostics on"); return; }
  if (!_bOn) return;
  Log("GLSTATE diagnostics on");
}

#else

void TakeGlUploadStats(int &ct, long long &llPixels, double &dMs) { ct = 0; llPixels = 0; dMs = 0.0; }
void EndGlFrame(bool) {}
void UpdateGlStateDiag() {}
void InstallGlStateDiag() {}

#endif
