// Crosshair: size on wide screens, and a fixed crosshair.
//
// CPlayerWeapons::RenderCrosshair (Entities.dll 0x60218510) casts a ray
// from the weapon and draws the crosshair where it hits, projected onto the
// screen; with nothing hit it goes to the middle.  Its size goes from width *
// 0.025 up close to width * 0.0094 far away, so on a wide screen it was as much
// too big as the HUD.  One instruction pair, where it reads the drawport width
// for the size (0x602188C1), now calls into the mod:
//  - with the HUD size fix on, the size uses min(width, height * 4/3), like the HUD;
//  - with the fixed crosshair on, the crosshair goes to the middle of the
//    screen wherever the ray hits.  Its size and colour still come from what
//    the ray hits.  Only in first person: the alpha's third-person camera is
//    behind and above the player, so what the weapon aims at shows above the
//    middle of the screen, and only the game's own crosshair shows where.
//  - with the First Encounter's third-person camera (thirdperson.cpp), which
//    looks at what the weapon aims at, it's in the middle in third person too.
// And the whole function runs with the player's projection widened like the
// view it draws over (Hor+, FOV setting).  It was projecting the hit point with
// the game's 90 degrees, so the crosshair sat too far from the middle: a little
// in first person, far off in third person, where the hit point is high up.
#include "alphafixes.h"

namespace {

// Entities.dll (13 Jan 2000 build, preferred base)
constexpr uintptr_t ENT_CROSSHAIR_SIZE = 0x602188C1;   // mov esi, [ebp + 0xC] (drawport); mov eax, [esi + 0x10] (width)
constexpr uintptr_t ENT_RENDER_CROSSHAIR = 0x60218510;  // CPlayerWeapons::RenderCrosshair(CProjection3D &, CDrawPort *, CPlacement3D &)
constexpr size_t PW_PLAYER = 0xD0;                      // CPlayerWeapons::m_penPlayer (the crosshair ray's origin entity)
constexpr size_t PL_VIEWMODE = 0x330;                   // CPlayer: 0 eyes, 3 third person

int _bScaleByHeight = 0;
int _bFixed = 0;
const float _fHalf = 0.5f;

using RenderCrosshair_t = void(__fastcall *)(void *ppw, void *pvEdx, void *ppr, void *pdp, void *ppl);
RenderCrosshair_t _pfnRenderCrosshair = nullptr;

void __fastcall RenderCrosshair_Hook(void *ppw, void *pvEdx, void *ppr, void *pdp, void *ppl)
{
  char *penPlayer = *(char **)((char *)ppw + PW_PLAYER);
  int iViewMode = penPlayer != nullptr ? *(int *)(penPlayer + PL_VIEWMODE) : -1;
  _bFixed = (g_cfg.bFixedCrosshair && iViewMode == 0) || (g_cfg.bTfeThirdPerson && iViewMode == 3);
  float fOld = WidenPlayerProjection(ppr);
  _pfnRenderCrosshair(ppw, pvEdx, ppr, pdp, ppl);
  RestorePlayerProjection(ppr, fOld);
}

// In RenderCrosshair's frame: [ebp + 0xC] the drawport (width +0x10, height
// +0x14), [ebp - 0x50] and [ebp - 0x4C] the crosshair's screen position.
// Returns the width to size the crosshair by in eax; the x87 stack is left as it was.
__declspec(naked) void CrosshairStub()
{
  __asm {
    mov   esi, [ebp + 0x0C]
    cmp   dword ptr [_bFixed], 0
    je    sizing
    fild  dword ptr [esi + 0x10]
    fmul  dword ptr [_fHalf]
    fstp  dword ptr [ebp - 0x50]
    fild  dword ptr [esi + 0x14]
    fmul  dword ptr [_fHalf]
    fstp  dword ptr [ebp - 0x4C]
  sizing:
    mov   eax, [esi + 0x10]
    cmp   dword ptr [_bScaleByHeight], 0
    je    finished
    mov   eax, [esi + 0x14]
    shl   eax, 2
    xor   edx, edx
    mov   ecx, 3
    div   ecx
    cmp   eax, [esi + 0x10]
    jle   finished
    mov   eax, [esi + 0x10]
  finished:
    ret
  }
}

bool _bInstalled = false;

}  // namespace

void InstallCrosshairEntitiesHooks()
{
  static const BYTE aubOld[] = { 0x8B, 0x75, 0x0C, 0x8B, 0x46, 0x10 };
  uintptr_t ul = RebaseEntities(ENT_CROSSHAIR_SIZE);
  BYTE aubNew[6] = { 0xE8, 0, 0, 0, 0, 0x90 };   // call CrosshairStub; nop
  int32_t slRel = (int32_t)((uintptr_t)CrosshairStub - (ul + 5));
  memcpy(aubNew + 1, &slRel, 4);
  _bInstalled = CodeMatches(ul, aubOld, sizeof(aubOld)) && WriteCode((void *)ul, aubNew, sizeof(aubNew));
  UpdateCrosshair();
  // push ebp; mov ebp, esp; push -1; push (handler)
  static const BYTE aubRender[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68 };
  _bInstalled &= CodeMatches(RebaseEntities(ENT_RENDER_CROSSHAIR), aubRender, sizeof(aubRender))
              && HookFunction((void *)RebaseEntities(ENT_RENDER_CROSSHAIR), (void *)RenderCrosshair_Hook,
                              (void **)&_pfnRenderCrosshair, "CPlayerWeapons::RenderCrosshair");
  Log("Crosshair fix: %s", _bInstalled ? "installed" : "Entities.dll code differs, not patched");
}

// After a settings change.  The fixed crosshair is decided per call (view mode).
void UpdateCrosshair()
{
  _bScaleByHeight = g_cfg.bFixHud ? 1 : 0;
}
