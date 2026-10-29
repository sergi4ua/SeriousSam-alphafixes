// Widescreen: Hor+ field of view and HUD size.
//
// The game sets a fixed horizontal FOV of 90 degrees for every view
// (CPlayer::RenderGameView, Entities.dll), so a wider screen shows *less* at the
// top and bottom than 4:3 does ("Vert-").  Here the FOV is widened instead: the
// vertical view of 4:3 is kept and the extra width shows more to the sides
// (Hor+).  This is done in Engine.dll's RenderView() and, for the weapon model
// that Entities.dll draws on top, in BeginModelRenderingView(): if the
// projection is a perspective one and its screen box is wider than 4:3, its
// FOV is widened for that call and put back afterwards.
//
// The HUD is scaled by screen width / 640, so on 16:9 it is a third bigger
// than on a 4:3 screen of the same height.  The fix scales it by
// min(width, height * 4/3) / 640 instead.
//
// The menu (SeriousSam.exe's DoMenu) lays itself out in fractions of the
// screen, so on a wide screen its 3D buttons and logos are stretched.  It is
// drawn into a centered 4:3 drawport instead, with black bars at the sides.
// The alpha's menu is keyboard-only, so there is no mouse mapping to adjust.
//
// The same hook fixes a bug of the game: DoMenu() also runs while the menu is
// hidden, drawing whatever menu items are still flagged visible, until none
// are.  Starting a level from Single player > New game hides the menu but
// leaves one item (a small triangle, likely the list's scroll arrow) flagged
// visible, so it stays in the middle of the screen during the whole game and
// in every menu opened later.  With a game running and the menu hidden, no
// menu item should be visible: their visible flags are cleared and DoMenu()
// stops.
#include "alphafixes.h"

#include <cmath>
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

namespace {

// Engine.dll layout (alpha build)
constexpr size_t APR_CURRENT_PROJECTION = 0x654;   // CAnyProjection3D::ap_CurrentProjection
constexpr size_t PR_SCREEN_BBOX = 0x54;            // CProjection3D::pr_ScreenBBox (min x, min y, max x, max y)
constexpr size_t PR_ASPECT_RATIO = 0x64;           // CProjection3D::pr_AspectRatio
constexpr size_t PR_FOV_WIDTH = 0x184;             // CPerspectiveProjection3D::ppr_FOVWidth

// Entities.dll, CPlayer::RenderGameView: _fHUDScale = width * (1/640)
constexpr uintptr_t ENT_HUDSCALE_STORE = 0x6020BA72;  // fstp dword ptr [_fHUDScale]
constexpr uintptr_t ENT_HUDSCALE = 0x6029ABFC;
constexpr uintptr_t ENT_PIXWIDTH = 0x6029AC08;     // _pixDPWidth (int)
constexpr uintptr_t ENT_PIXHEIGHT = 0x6029AC04;    // _pixDPHeight (int)
constexpr float GAME_FOV = 90.0f;

// SeriousSam.exe
constexpr uintptr_t EXE_DOMENU = 0x415C60;         // BOOL DoMenu(CDrawPort *pdp)
const int *const _pbMenuActive = (const int *)0x441178;   // menu shown (the game isn't drawn)
const int *const _pbGameActive = (const int *)0x441170;   // a game is running
// first CListNode of all menu gadgets (CMenuGadget + 0x10); next node at +0, mg_bVisible at +8
char **const _ppMenuGadgets = (char **)0x43D2F0;

using RenderView_t = void(__cdecl *)(void *pwo, void *penViewer, void *papr, void *pdp);
using DoMenu_t = BOOL(__cdecl *)(void *pdp);
using DrawPortSub_t = void(__fastcall *)(void *pdp, void *pvEdx, void *pdpOriginal, double dMinI, double dMinJ,
                                         double dSizeI, double dSizeJ);
using DrawPortMethod_t = int(__fastcall *)(void *pdp, void *pvEdx);
using DrawPortFill_t = void(__fastcall *)(void *pdp, void *pvEdx, ULONG ulColor);
using BeginModelRenderingView_t = void(__cdecl *)(void *papr, void *pdp);

RenderView_t _pfnRenderView = nullptr;
BeginModelRenderingView_t _pfnBeginModelRenderingView = nullptr;
void *_pvtblPerspective = nullptr;
float _fLastAspect = 4.0f / 3.0f;

DoMenu_t _pfnDoMenu = nullptr;
DrawPortSub_t _pfnDrawPortSub = nullptr;
DrawPortMethod_t _pfnDrawPortDtor = nullptr;
DrawPortMethod_t _pfnDrawPortLock = nullptr;
DrawPortMethod_t _pfnDrawPortUnlock = nullptr;
DrawPortMethod_t _pfnDrawPortWidth = nullptr;
DrawPortMethod_t _pfnDrawPortHeight = nullptr;
DrawPortFill_t _pfnDrawPortFill = nullptr;

uintptr_t _ulEntitiesBase = 0;
uintptr_t _ulEntitiesEnd = 0;
bool _bHudPatched = false;
float *_pfHudScale = nullptr;
int *_piPixWidth = nullptr;
int *_piPixHeight = nullptr;

bool IsFromEntities(void *pvReturn)
{
  uintptr_t ul = (uintptr_t)pvReturn;
  return _ulEntitiesBase != 0 && ul >= _ulEntitiesBase && ul < _ulEntitiesEnd;
}

float DegToRad(float f) { return f * 3.14159265f / 180.0f; }
float RadToDeg(float f) { return f * 180.0f / 3.14159265f; }

// Widens a perspective projection for this call; returns the FOV to restore, or NAN.
float WidenPerspective(char *pr, bool bUserFov)
{
  if (pr == nullptr || *(void **)pr != _pvtblPerspective) return NAN;

  const float *pfBox = (const float *)(pr + PR_SCREEN_BBOX);
  float fWidth = pfBox[2] - pfBox[0];
  float fHeight = pfBox[3] - pfBox[1];
  float fPixelAspect = *(const float *)(pr + PR_ASPECT_RATIO);
  if (!(fWidth > 0.0f && fHeight > 0.0f)) return NAN;
  if (!(fPixelAspect > 0.0f)) fPixelAspect = 1.0f;

  float *pfFov = (float *)(pr + PR_FOV_WIDTH);
  float fOld = *pfFov;
  float fFov = fOld;
  float fAspect = fWidth / (fHeight * fPixelAspect);
  if (bUserFov) _fLastAspect = fAspect;

  // the player's normal view uses exactly 90; zoomed views keep their own FOV
  if (bUserFov && fabsf(fFov - GAME_FOV) < 0.01f) fFov = g_cfg.fFov;
  if (g_cfg.bHorPlusFov && fAspect > 4.0f / 3.0f + 0.01f) {
    float fHalf = tanf(DegToRad(fFov * 0.5f)) * fAspect / (4.0f / 3.0f);
    fFov = 2.0f * RadToDeg(atanf(fHalf));
  }
  if (fFov > 170.0f) fFov = 170.0f;
  if (fFov == fOld) return NAN;
  *pfFov = fFov;
  return fOld;
}

float WidenProjection(void *papr, bool bUserFov)
{
  return WidenPerspective(*(char **)((char *)papr + APR_CURRENT_PROJECTION), bUserFov);
}

void RestoreProjection(void *papr, float fOld)
{
  if (std::isnan(fOld)) return;
  char *pr = *(char **)((char *)papr + APR_CURRENT_PROJECTION);
  *(float *)(pr + PR_FOV_WIDTH) = fOld;
}

void __cdecl RenderView_Hook(void *pwo, void *penViewer, void *papr, void *pdp)
{
  UpdateLateHooks();
  bool bPlayerView = IsFromEntities(_ReturnAddress());
  float fOld = WidenProjection(papr, bPlayerView);
  _pfnRenderView(pwo, penViewer, papr, pdp);
  RestoreProjection(papr, fOld);
}

// The weapon model: Entities.dll draws it with its own projection after the world.
// The menu (SeriousSam.exe) also draws models this way; it keeps its look.
void __cdecl BeginModelRenderingView_Hook(void *papr, void *pdp)
{
  UpdateLateHooks();
  if (!IsFromEntities(_ReturnAddress())) {
    _pfnBeginModelRenderingView(papr, pdp);
    return;
  }
  // BeginModelRenderingView prepares the projection and copies it, so it can be restored right away
  float fOld = WidenProjection(papr, false);
  _pfnBeginModelRenderingView(papr, pdp);
  RestoreProjection(papr, fOld);
}

// ---------------------------------------------------------------------------
// Menu

BOOL __cdecl DoMenu_Hook(void *pdp)
{
  if (!*_pbMenuActive && *_pbGameActive) {
    for (char *pln = *_ppMenuGadgets; pln != nullptr && *(char **)pln != nullptr; pln = *(char **)pln) {
      *(int *)(pln + 8) = FALSE;
    }
    return FALSE;   // tells the main loop the menu is done drawing
  }

  int iWidth = _pfnDrawPortWidth(pdp, nullptr);
  int iHeight = _pfnDrawPortHeight(pdp, nullptr);
  if (!g_cfg.bMenu43 || iHeight <= 0 || iWidth <= iHeight * 4 / 3 + 2) return _pfnDoMenu(pdp);

  // a CDrawPort is about 0x90 bytes; the menu runs on the main thread only
  alignas(16) static char _achSubDrawPort[0x400];
  double dSizeI = (iHeight * 4.0 / 3.0) / iWidth;
  // black bars only over the menu's own background, never over a game view
  if (*_pbMenuActive) _pfnDrawPortFill(pdp, nullptr, 0x000000FF);
  _pfnDrawPortSub(_achSubDrawPort, nullptr, pdp, (1.0 - dSizeI) * 0.5, 0.0, dSizeI, 1.0);
  BOOL bResult;
  if (_pfnDrawPortLock(_achSubDrawPort, nullptr)) {
    bResult = _pfnDoMenu(_achSubDrawPort);
    _pfnDrawPortUnlock(_achSubDrawPort, nullptr);
  } else {
    bResult = _pfnDoMenu(pdp);
  }
  _pfnDrawPortDtor(_achSubDrawPort, nullptr);
  return bResult;
}

bool InstallMenuPillarbox()
{
  static const BYTE aubDoMenu[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x74, 0x56, 0x42, 0x00 };
  _pfnDrawPortSub = (DrawPortSub_t)EngineProc("??0CDrawPort@@QAE@PAV0@NNNN@Z");
  _pfnDrawPortDtor = (DrawPortMethod_t)EngineProc("??1CDrawPort@@QAE@XZ");
  _pfnDrawPortLock = (DrawPortMethod_t)EngineProc("?Lock@CDrawPort@@QAEHXZ");
  _pfnDrawPortUnlock = (DrawPortMethod_t)EngineProc("?Unlock@CDrawPort@@QAEXXZ");
  _pfnDrawPortWidth = (DrawPortMethod_t)EngineProc("?GetWidth@CDrawPort@@QBEJXZ");
  _pfnDrawPortHeight = (DrawPortMethod_t)EngineProc("?GetHeight@CDrawPort@@QBEJXZ");
  _pfnDrawPortFill = (DrawPortFill_t)EngineProc("?Fill@CDrawPort@@QBEXK@Z");
  if (!_pfnDrawPortSub || !_pfnDrawPortDtor || !_pfnDrawPortLock || !_pfnDrawPortUnlock || !_pfnDrawPortWidth
   || !_pfnDrawPortHeight || !_pfnDrawPortFill) {
    return false;
  }
  if (!CodeMatches(EXE_DOMENU, aubDoMenu, sizeof(aubDoMenu))) {
    Log("Menu 4:3: code differs, not patched");
    return false;
  }
  return HookFunction((void *)EXE_DOMENU, (void *)DoMenu_Hook, (void **)&_pfnDoMenu, "DoMenu");
}

// ---------------------------------------------------------------------------
// HUD scale

extern "C" void __cdecl ComputeHudScale(float fOriginal)
{
  float fWidth = (float)*_piPixWidth;
  float fHeight = (float)*_piPixHeight;
  float fScale = fOriginal;
  if (g_cfg.bFixHud && fHeight > 0.0f && fWidth > fHeight * 4.0f / 3.0f) {
    fScale = fHeight * (4.0f / 3.0f) / 640.0f;
  }
  *_pfHudScale = fScale;
}

// Replaces "fstp dword ptr [_fHUDScale]" (st0 = width / 640).
__declspec(naked) void HudScaleStub()
{
  __asm {
    sub   esp, 4
    fstp  dword ptr [esp]
    pushad
    push  dword ptr [esp + 32]
    call  ComputeHudScale
    add   esp, 4
    popad
    add   esp, 4
    ret
  }
}

void PatchEntitiesHud()
{
  uintptr_t ulStore = RebaseEntities(ENT_HUDSCALE_STORE);
  uint32_t ulScaleAddr = (uint32_t)RebaseEntities(ENT_HUDSCALE);

  BYTE aubOld[6] = { 0xD9, 0x1D };                 // fstp dword ptr [_fHUDScale]
  memcpy(aubOld + 2, &ulScaleAddr, 4);
  if (!CodeMatches(ulStore, aubOld, sizeof(aubOld))) {
    Log("HUD fix: Entities.dll code differs, not patched");
    return;
  }
  _pfHudScale = (float *)RebaseEntities(ENT_HUDSCALE);
  _piPixWidth = (int *)RebaseEntities(ENT_PIXWIDTH);
  _piPixHeight = (int *)RebaseEntities(ENT_PIXHEIGHT);

  BYTE aubNew[6] = { 0xE8, 0, 0, 0, 0, 0x90 };     // call HudScaleStub; nop
  int32_t slRel = (int32_t)((uintptr_t)HudScaleStub - (ulStore + 5));
  memcpy(aubNew + 1, &slRel, 4);
  _bHudPatched = WriteCode((void *)ulStore, aubNew, sizeof(aubNew));
  Log("HUD fix: %s", _bHudPatched ? "installed" : "failed");
}

}  // namespace

bool InstallWidescreenFixes()
{
  _pvtblPerspective = EngineProc("??_7CPerspectiveProjection3D@@6B@");
  if (_pvtblPerspective == nullptr) return false;

  bool bOk = HookFunction(EngineProc("?RenderView@@YAXAAVCWorld@@AAVCEntity@@AAVCAnyProjection3D@@AAVCDrawPort@@@Z"),
                          (void *)RenderView_Hook, (void **)&_pfnRenderView, "RenderView");
  bOk &= HookFunction(EngineProc("?BeginModelRenderingView@@YAXAAVCAnyProjection3D@@PAVCDrawPort@@@Z"),
                      (void *)BeginModelRenderingView_Hook, (void **)&_pfnBeginModelRenderingView,
                      "BeginModelRenderingView");
  bOk &= InstallMenuPillarbox();
  return bOk;
}

void InstallWidescreenEntitiesHooks(HMODULE hEntities)
{
  auto *pnt = (IMAGE_NT_HEADERS *)((BYTE *)hEntities + ((IMAGE_DOS_HEADER *)hEntities)->e_lfanew);
  _ulEntitiesBase = (uintptr_t)hEntities;
  _ulEntitiesEnd = _ulEntitiesBase + pnt->OptionalHeader.SizeOfImage;
  PatchEntitiesHud();
}

float LastViewAspect() { return _fLastAspect; }

// The crosshair projects what the weapon aims at with the player's view
// projection (a CPerspectiveProjection3D), after the view was drawn; it has to
// be widened like the view, or everything off the middle of the screen lands
// too far out (by 1.9 times at 16:9 with a 110 degree FOV).
float WidenPlayerProjection(void *pr)
{
  return WidenPerspective((char *)pr, true);
}

void RestorePlayerProjection(void *pr, float fOld)
{
  if (!std::isnan(fOld)) *(float *)((char *)pr + PR_FOV_WIDTH) = fOld;
}
