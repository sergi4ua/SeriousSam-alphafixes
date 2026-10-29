// Light crashes.
//
// Two crash reports from a beta tester, both while going between Tomb of
// Menes and King Valley, both in the renderer's lens flare pass (Engine.dll
// 0x600936F0) on the same kind of light:
//
//   GameCrash_Jarmen_27102026_1448.RPT
//     -> CLightSource::GetLightColor -> CAnimObject::GetFrame: the light's
//        animation pointer (CLightSource::ls_paoLightAnimation, +0x34) held
//        garbage (0xEE1FA455)
//   SeriousSam_Jarmen_27102026_2127.RPT (with the first fix below)
//     -> the light's lens flare pointer (ls_plftLensFlare, +0x30) held garbage
//        (0xDD5BF661); the log shows the animation pointer was 0xEE1FA455 again
//
// Both pointers are left unset by CLightSource's constructor; every entity
// fills them when it sets its light up.  So this was a light that was never
// set up but still handed to the renderer.  The player is such a case:
// CPlayer::GetLightSource() returns the player's light whenever bPlayerLights
// is on (it is by default), but CPlayer has no Copy() of its own, so when the
// player is carried into the next level its copy has a light straight from
// the constructor until the player handles the level change and sets it up
// again.  Meanwhile CWorld::CopyEntities() looks for the light's shadows, and
// the frames drawn in between look for its lens flare.  The flare is kept when
// the player's feet are in front of the camera (looking down); then the
// garbage pointers crash the lens flare pass.  In tests the copy's pointers
// held values like 0xFBE3FBE3, and with the second report's values put there
// (testing aid in testaids.cpp) the game crashed at the reported instruction
// with the reported registers.
//
// The fixes:
//  - CPlayer::GetLightSource() returns no light until the light is set up for
//    this player.  Every light setup in Entities.dll stores the owner
//    (ls_penEntity, +0x0C); a light whose owner is some other value has not
//    been set up.
//  - The renderer adds a lens flare only for a light whose owner is the entity
//    it came from, for any other entity class with the same problem.
//  - The three functions that read a light's animation run inside an exception
//    handler.  If reading the animation faults, the light's animation pointer
//    is cleared and the call repeated: the light keeps its plain color instead
//    of the game crashing.
#include "alphafixes.h"
#include <intrin.h>

namespace {

constexpr size_t LS_ENTITY = 0x0C;        // CLightSource::ls_penEntity
constexpr size_t LS_LENSFLARE = 0x30;     // CLightSource::ls_plftLensFlare
constexpr size_t LS_ANIMATION = 0x34;     // CLightSource::ls_paoLightAnimation

// Engine.dll (alpha build)
constexpr uintptr_t ENG_ADDLENSFLARE = 0x60094BE0;   // CRenderer::AddLensFlare(CEntity *, CLightSource *, CProjection3D *)
constexpr uintptr_t ENG_LENSFLARE_GETLIGHTSOURCE = 0x60094B2B;   // return address of its caller's GetLightSource() call
// Entities.dll (13 Jan 2000 build, preferred base)
constexpr uintptr_t ENT_PLAYER_GETLIGHTSOURCE = 0x6020B800;   // CPlayer::GetLightSource

// Engine.dll (alpha build): the main renderer's lens flares, for the testing aid
constexpr uintptr_t ENG_RENDERER_LENSFLARES = 0x60111B60 + 0xDF8;   // array pointer, then count; 0x38-byte entries

using GetLightColor_t = void(__fastcall *)(char *pls, void *pvEdx, BYTE *pubR, BYTE *pubG, BYTE *pubB);
using GetLightColorAndAmbient_t = void(__fastcall *)(char *pls, void *pvEdx, BYTE *pubR, BYTE *pubG, BYTE *pubB,
                                                     BYTE *pubAR, BYTE *pubAG, BYTE *pubAB);
using GetCurrentFrame_t = ULONG(__fastcall *)(char *pls, void *pvEdx);

GetLightColor_t _pfnGetLightColor = nullptr;
GetLightColorAndAmbient_t _pfnGetLightColorAndAmbient = nullptr;
GetCurrentFrame_t _pfnGetCurrentFrame = nullptr;
long _ctRecovered = 0;

using AddLensFlare_t = void(__fastcall *)(void *pre, void *pvEdx, char *pen, char *pls, void *ppr);
using GetLightSource_t = char *(__fastcall *)(char *pen, void *pvEdx);
AddLensFlare_t _pfnAddLensFlare = nullptr;
GetLightSource_t _pfnPlayerGetLightSource = nullptr;
long _ctUnsetLights = 0;
const char *_plsLastUnset = nullptr;
#if AF_TEST_AIDS
volatile bool _bPoisonUnsetPlayerLight = false;   // testing aid, see PoisonUnsetPlayerLight()
#endif

bool IsSetUpFor(const char *pls, const char *pen)
{
  return *(const char *const *)(pls + LS_ENTITY) == pen;
}

// Once per light: it is asked for every frame until its entity sets it up.
void LogUnsetLight(const char *strWhere, const char *pls, const char *pen, const void *pvCaller)
{
  if (pls == _plsLastUnset || _ctUnsetLights >= 20) return;
  _plsLastUnset = pls;
  _ctUnsetLights++;
  Log("Light fix: %s: light %p of entity %p is not set up (owner %p, lens flare %p, animation %p, asked from %p); left out",
      strWhere, (const void *)pls, (const void *)pen, *(void *const *)(pls + LS_ENTITY),
      *(void *const *)(pls + LS_LENSFLARE), *(void *const *)(pls + LS_ANIMATION), pvCaller);
}

int AccessViolationOnly(DWORD dwCode)
{
  return dwCode == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

// After a fault inside the engine: the x87 stack may hold the aborted function's
// values.  Calls start with it empty, so reset it and keep the control word.
void RecoverLight(char *pls, unsigned short uwControl)
{
  __asm {
    fninit
    fldcw uwControl
  }
  void *pvOld = *(void **)(pls + LS_ANIMATION);
  *(void **)(pls + LS_ANIMATION) = nullptr;
  if (_ctRecovered++ < 20) Log("Light fix: light %p had an invalid animation pointer %p; cleared it", (void *)pls, pvOld);
}

bool TryGetLightColor(char *pls, BYTE *pubR, BYTE *pubG, BYTE *pubB)
{
  __try {
    _pfnGetLightColor(pls, nullptr, pubR, pubG, pubB);
    return true;
  } __except (AccessViolationOnly(GetExceptionCode())) {
    return false;
  }
}

bool TryGetLightColorAndAmbient(char *pls, BYTE *pubR, BYTE *pubG, BYTE *pubB, BYTE *pubAR, BYTE *pubAG, BYTE *pubAB)
{
  __try {
    _pfnGetLightColorAndAmbient(pls, nullptr, pubR, pubG, pubB, pubAR, pubAG, pubAB);
    return true;
  } __except (AccessViolationOnly(GetExceptionCode())) {
    return false;
  }
}

bool TryGetCurrentFrame(char *pls, ULONG &ulFrame)
{
  __try {
    ulFrame = _pfnGetCurrentFrame(pls, nullptr);
    return true;
  } __except (AccessViolationOnly(GetExceptionCode())) {
    return false;
  }
}

// void CLightSource::GetLightColor(UBYTE &ubR, UBYTE &ubG, UBYTE &ubB)
void __fastcall GetLightColor_Hook(char *pls, void *pvEdx, BYTE *pubR, BYTE *pubG, BYTE *pubB)
{
  unsigned short uwControl;
  __asm fnstcw uwControl
  if (TryGetLightColor(pls, pubR, pubG, pubB)) return;
  RecoverLight(pls, uwControl);
  _pfnGetLightColor(pls, pvEdx, pubR, pubG, pubB);
}

// void CLightSource::GetLightColorAndAmbient(UBYTE &ubR, ..., UBYTE &ubAB)
void __fastcall GetLightColorAndAmbient_Hook(char *pls, void *pvEdx, BYTE *pubR, BYTE *pubG, BYTE *pubB, BYTE *pubAR,
                                             BYTE *pubAG, BYTE *pubAB)
{
  unsigned short uwControl;
  __asm fnstcw uwControl
  if (TryGetLightColorAndAmbient(pls, pubR, pubG, pubB, pubAR, pubAG, pubAB)) return;
  RecoverLight(pls, uwControl);
  _pfnGetLightColorAndAmbient(pls, pvEdx, pubR, pubG, pubB, pubAR, pubAG, pubAB);
}

// COLOR CLightSource::GetCurrentFrame(void)
ULONG __fastcall GetCurrentFrame_Hook(char *pls, void *pvEdx)
{
  unsigned short uwControl;
  __asm fnstcw uwControl
  ULONG ulFrame;
  if (TryGetCurrentFrame(pls, ulFrame)) return ulFrame;
  RecoverLight(pls, uwControl);
  return _pfnGetCurrentFrame(pls, pvEdx);
}

// void CRenderer::AddLensFlare(CEntity *pen, CLightSource *pls, CProjection3D *ppr)
void __fastcall AddLensFlare_Hook(void *pre, void *pvEdx, char *pen, char *pls, void *ppr)
{
  if (!IsSetUpFor(pls, pen)) {
    LogUnsetLight("lens flare", pls, pen, _ReturnAddress());
    return;
  }
  _pfnAddLensFlare(pre, pvEdx, pen, pls, ppr);
}

// CLightSource *CPlayer::GetLightSource(void)
char *__fastcall PlayerGetLightSource_Hook(char *pen, void *pvEdx)
{
  char *pls = _pfnPlayerGetLightSource(pen, pvEdx);
  if (pls != nullptr && !IsSetUpFor(pls, pen)) {
#if AF_TEST_AIDS
    if (_bPoisonUnsetPlayerLight && (uintptr_t)_ReturnAddress() == ENG_LENSFLARE_GETLIGHTSOURCE) {
      _bPoisonUnsetPlayerLight = false;
      *(char **)(pls + LS_ENTITY) = pen;
      *(ULONG *)(pls + LS_LENSFLARE) = 0xDD5BF661;
      *(ULONG *)(pls + LS_ANIMATION) = 0xEE1FA455;
      Log("Bad light: player light %p not set up; gave it the reported lens flare and animation pointers", (void *)pls);
      return pls;
    }
#endif
    LogUnsetLight("player", pls, pen, _ReturnAddress());
    return nullptr;
  }
  return pls;
}

}  // namespace

void InstallLightFix()
{
  if (!g_cfg.bLightFix) return;
  bool bOk = HookFunction(EngineProc("?GetLightColor@CLightSource@@QAEXAAE00@Z"), (void *)GetLightColor_Hook,
                          (void **)&_pfnGetLightColor, "CLightSource::GetLightColor");
  bOk &= HookFunction(EngineProc("?GetLightColorAndAmbient@CLightSource@@QAEXAAE00000@Z"),
                      (void *)GetLightColorAndAmbient_Hook, (void **)&_pfnGetLightColorAndAmbient,
                      "CLightSource::GetLightColorAndAmbient");
  bOk &= HookFunction(EngineProc("?GetCurrentFrame@CLightSource@@QAEKXZ"), (void *)GetCurrentFrame_Hook,
                      (void **)&_pfnGetCurrentFrame, "CLightSource::GetCurrentFrame");
  // push ebp; mov ebp, esp; sub esp, 0x30; mov eax, [lens flare setting]
  static const BYTE aubAddLensFlare[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x30, 0xA1, 0xE8, 0x19, 0x11, 0x60 };
  if (CodeMatches(ENG_ADDLENSFLARE, aubAddLensFlare, sizeof(aubAddLensFlare))) {
    bOk &= HookFunction((void *)ENG_ADDLENSFLARE, (void *)AddLensFlare_Hook, (void **)&_pfnAddLensFlare,
                        "CRenderer::AddLensFlare");
  } else {
    bOk = false;
  }
  Log("Light fix: %s", bOk ? "installed" : "partly failed");
}

void InstallLightFixEntitiesHooks(HMODULE hEntities)
{
  (void)hEntities;
  if (!g_cfg.bLightFix) return;
  // mov eax, [bPlayerLights] (address relocated, not compared); test eax, eax; je; lea eax, [ecx + 0x9B0]; ret
  static const BYTE aubGetLightSource[] = { 0x85, 0xC0, 0x74, 0x07, 0x8D, 0x81, 0xB0, 0x09, 0x00, 0x00, 0xC3 };
  uintptr_t ulGetLightSource = RebaseEntities(ENT_PLAYER_GETLIGHTSOURCE);
  bool bOk = *(const BYTE *)ulGetLightSource == 0xA1
          && CodeMatches(ulGetLightSource + 5, aubGetLightSource, sizeof(aubGetLightSource))
          && HookFunction((void *)ulGetLightSource, (void *)PlayerGetLightSource_Hook, (void **)&_pfnPlayerGetLightSource,
                          "CPlayer::GetLightSource");
  Log("Light fix (player light): %s", bOk ? "installed" : "failed");
}

#if AF_TEST_AIDS
// Testing aid: the next time the renderer looks for lens flares on the player
// before the player's light is set up (just after a level change), the light gets the lens flare and animation
// pointers of the second crash report and is let through, with its owner set so
// the lens flare check passes too.  The animation fault is recovered and then
// the game crashes on the lens flare like in the report.
void PoisonUnsetPlayerLight()
{
  _bPoisonUnsetPlayerLight = true;
}

// Testing aid: gives the light behind the main renderer's first lens flare the
// animation pointer from the crash report.  Without the fix the game crashes
// like in the report.
bool BreakLensFlareLight()
{
  char *pEntries = *(char **)ENG_RENDERER_LENSFLARES;
  int ctEntries = *(int *)(ENG_RENDERER_LENSFLARES + 4);
  if (ctEntries <= 0 || pEntries == nullptr) return false;
  char *pls = *(char **)(pEntries + 4);
  if (pls == nullptr) return false;
  *(ULONG *)(pls + LS_ANIMATION) = 0xEE1FA455;
  return true;
}
#endif  // AF_TEST_AIDS
