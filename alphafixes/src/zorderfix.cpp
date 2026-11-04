// Brushes that cut through each other drawn in the wrong order.
//
// Where a pillar of one brush stands in the floor of another, the part of the
// pillar below the floor showed through the floor (seen in Mental Tomb,
// converted from The First Encounter; the alpha's own levels avoid such
// overlaps).  The engine finds the visible brush polygons with its span
// buffer, which sorts polygons by depth only where their edges start on a
// scan line; it can't tell which of two polygons that cross each other is in
// front.  Serious Sam: The First Encounter gets away with that because it then
// draws the visible polygons with the depth test on.  The alpha draws them
// with the depth test set to GL_ALWAYS: in RenderScene() (Engine.dll
// 0x6002D670) the first texture pass of opaque polygons uses GL_LEQUAL only
// while a flag in CGfxLibrary (+0x84) is set, which the engine does only while
// it renders the view in a mirror.  The later passes (texture layers, shadows)
// already test against the depth the first pass writes.  Before drawing, the
// renderer fills the depth buffer with 0.0, the nearest value, so that models
// can't show through gaps between polygons.
//
// The fix: the main world pass (the RenderScene() call at 0x60096694 in the
// renderer's draw function, after the background and its models) gets the
// mirror's treatment.  The depth buffer is filled with 1.0 (the farthest
// value, as the engine does after each frame) and the flag is set for that
// call, so the opaque polygons are drawn with GL_LEQUAL, as in TFE.  The
// background world, models, translucent polygons and mirrors are drawn as
// before.
#include "alphafixes.h"

namespace {

constexpr uintptr_t ENG_MAINPASS_CALL = 0x60096694;  // call RenderScene: the main world's opaque polygons
constexpr uintptr_t ENG_RENDERSCENE = 0x6002D670;
constexpr uintptr_t ENG_DEPTHFLAG_TEST = 0x6002D72A;  // RenderScene: mov eax, [_pGfx]; cmp [eax + 0x84], esi
constexpr size_t GFX_DEPTHTEST = 0x84;                // CGfxLibrary: opaque world polygons use GL_LEQUAL

// void RenderScene(CDrawPort *pdp, ScenePolygon *pspoFirst, FLOAT, FLOAT, FLOAT, COLOR)
typedef void (__cdecl *RenderScene_t)(void *pdp, void *pspoFirst, float f0, float f1, float f2, ULONG col);
// void CDrawPort::FillZBuffer(FLOAT zval) const (__thiscall)
typedef void (__fastcall *FillZBuffer_t)(void *pdp, void *pvEdx, float fZ);

RenderScene_t _pfnRenderScene;
FillZBuffer_t _pfnFillZBuffer;
void **_ppGfx;                                        // _pGfx

void __cdecl RenderMainScene(void *pdp, void *pspoFirst, float f0, float f1, float f2, ULONG col)
{
  int *piDepthTest = (int *)((char *)*_ppGfx + GFX_DEPTHTEST);
  // inside a mirror the engine has done this already
  if (*piDepthTest != 0) {
    _pfnRenderScene(pdp, pspoFirst, f0, f1, f2, col);
    return;
  }
  _pfnFillZBuffer(pdp, nullptr, 1.0f);
  *piDepthTest = 1;
  _pfnRenderScene(pdp, pspoFirst, f0, f1, f2, col);
  *piDepthTest = 0;
}

}  // namespace

void InstallZOrderFix()
{
  if (!g_cfg.bZOrderFix) return;
  _pfnRenderScene = (RenderScene_t)EngineProc("?RenderScene@@YAXPAVCDrawPort@@PAUScenePolygon@@MMMK@Z");
  _pfnFillZBuffer = (FillZBuffer_t)EngineProc("?FillZBuffer@CDrawPort@@QBEXM@Z");
  _ppGfx = (void **)EngineProc("?_pGfx@@3PAVCGfxLibrary@@A");
  // call RenderScene
  BYTE aubCall[5] = { 0xE8 };
  LONG slRel = (LONG)(ENG_RENDERSCENE - (ENG_MAINPASS_CALL + 5));
  memcpy(aubCall + 1, &slRel, 4);
  // mov eax, [_pGfx]; cmp [eax + 0x84], esi
  static const BYTE aubFlagTest[] = { 0xA1, 0x04, 0xD6, 0x10, 0x60, 0x39, 0xB0, 0x84, 0x00, 0x00, 0x00 };
  bool bOk = _pfnRenderScene == (RenderScene_t)ENG_RENDERSCENE && _pfnFillZBuffer != nullptr && _ppGfx != nullptr
          && CodeMatches(ENG_MAINPASS_CALL, aubCall, sizeof(aubCall))
          && CodeMatches(ENG_DEPTHFLAG_TEST, aubFlagTest, sizeof(aubFlagTest));
  if (bOk) {
    slRel = (LONG)((uintptr_t)RenderMainScene - (ENG_MAINPASS_CALL + 5));
    memcpy(aubCall + 1, &slRel, 4);
    bOk = WriteCode((void *)ENG_MAINPASS_CALL, aubCall, sizeof(aubCall));
  }
  Log("Z-order fix: %s", bOk ? "installed" : "failed");
}
