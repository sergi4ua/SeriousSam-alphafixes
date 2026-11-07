// Brushes that cut through each other drawn in the wrong order.
//
// In levels converted from The First Encounter, a pillar or door of one brush
// standing in the floor of another showed through the floor: the part of the
// pillar below the floor was drawn over it.  The alpha's own levels avoid such
// overlaps.  Two things in the renderer cause it:
//
// 1. The renderer finds the visible brush polygons with its span buffer
//    (ASER), which sorts polygons by depth only where their edges start on a
//    scan line; it can't tell which of two polygons that cross each other is
//    in front.  Only the spans it picks are drawn (the alpha draws spans, not
//    whole polygons), so a floor span given to the pillar is simply missing.
//    TFE keeps such geometry out of the span buffer: polygons marked "detail"
//    (BPOF_DETAILPOLYGON, 1<<21, set by the level designer) don't take part in
//    the visibility test and are drawn whole with the depth test
//    (CRenderer::MakeDetailScreenPolygons() in Serious Engine 1).  The alpha
//    has the flag but never looks at it.
// 2. The first texture pass of opaque world polygons is drawn with the depth
//    test set to GL_ALWAYS: RenderScene() (Engine.dll 0x6002D670) uses
//    GL_LEQUAL only while a flag in CGfxLibrary (+0x84) is set, which the
//    engine does only while rendering the view in a mirror.  The later passes
//    (texture layers, shadows) already test against the depth the first pass
//    writes.  Before drawing, the renderer fills the depth buffer with 0.0, the
//    nearest value, so that models can't show through gaps between polygons.
//
// The fixes:
//  - Detail polygons that aren't portals get no edges in the span buffer: the
//    call that adds a screen edge (0x60097CA0, from AddScreenEdges() at
//    0x6008FC3B) skips their edges.  The detail polygons of each sector added
//    (AddActiveSector(), 0x60091E7D) are remembered, and after the scan
//    (0x60096C14 in CRenderer::Render()) each one is rasterized on its own
//    with the routine the engine uses for translucent portals, which also
//    stay out of the span buffer (0x60091AF0, 0x60091B10 for each edge,
//    0x60091BE0, as in PassPortal() at 0x60091990), with the current scan line
//    set to the top: its whole area becomes spans.  (Not while adding the
//    sector: the scan starts by resetting the rasterizer, after the viewer's
//    own sectors have been added.)  Background brushes, and views where the
//    renderer only computes shadows, are left as they were (TFE also keeps
//    detail polygons in the span buffer for shadows).
//  - The main world pass (the RenderScene() call at 0x60096694 in the
//    renderer's draw function, after the background and its models) gets the
//    mirror's treatment: the depth buffer is filled with 1.0 (the farthest
//    value, as the engine does after each frame) and the flag is set for that
//    call, so the opaque polygons, the detail ones included, are drawn with
//    GL_LEQUAL, as in TFE.  The background world, models, translucent polygons
//    and mirrors are drawn as before.
#include "alphafixes.h"

namespace {

constexpr uintptr_t ENG_MAINPASS_CALL = 0x60096694;  // call RenderScene: the main world's opaque polygons
constexpr uintptr_t ENG_RENDERSCENE = 0x6002D670;
constexpr uintptr_t ENG_DEPTHFLAG_TEST = 0x6002D72A;  // RenderScene: mov eax, [_pGfx]; cmp [eax + 0x84], esi
constexpr size_t GFX_DEPTHTEST = 0x84;                // CGfxLibrary: opaque world polygons use GL_LEQUAL

constexpr uintptr_t ENG_ADDEDGE_CALL = 0x6008FC3B;     // AddScreenEdges: call AddEdge(sed)
constexpr uintptr_t ENG_ADDEDGE = 0x60097CA0;
constexpr uintptr_t ENG_ADDSCREENEDGES_CALL = 0x60091E7D;  // AddActiveSector: call AddScreenEdges()
constexpr uintptr_t ENG_ADDSCREENEDGES = 0x6008F940;
constexpr uintptr_t ENG_RASTER_BEGIN = 0x60091AF0;     // PassPortal's rasterizer: begin(spo), edge(sed), end(spo)
constexpr uintptr_t ENG_RASTER_EDGE = 0x60091B10;
constexpr uintptr_t ENG_RASTER_END = 0x60091BE0;
constexpr uintptr_t ENG_RASTER_DEPTH = 0x60111A8C;     // PassPortal counts these around the rasterizer
constexpr uintptr_t ENG_SCREENEDGES = 0x60111B0C;      // CScreenEdge * array (re_asedScreenEdges)
constexpr uintptr_t ENG_CTSCREENEDGES = 0x60111B18;    // its count this frame
constexpr uintptr_t ENG_SCAN_CALL = 0x60096C14;        // CRenderer::Render: call the scan (ScanEdges and cleanup)
constexpr uintptr_t ENG_SCAN = 0x60096350;
constexpr uintptr_t ENG_INIT_CALL = 0x60096C06;        // CRenderer::Render: call the per-frame initialization
constexpr uintptr_t ENG_INIT = 0x60095C00;

// CRenderer
constexpr size_t RE_RENDERINGSHADOWS = 0xD3C;
constexpr size_t RE_CURRENTSECTOR = 0xD84;
constexpr size_t RE_SCREENPOLYGONS = 0xD94;            // CScreenPolygon * array
constexpr size_t RE_TOPSCANLINE = 0xE48;
constexpr size_t RE_CURRENTSCANLINE = 0xE74;
// CBrushSector: its screen polygons in the renderer's array
constexpr size_t BSC_ISPO0 = 0x94;
constexpr size_t BSC_CTSPO = 0x98;
constexpr size_t BSC_BRUSHMIP = 0x38;
constexpr size_t BM_BRUSH = 0x18;
constexpr size_t BR_ENTITY = 0x660;
constexpr size_t EN_FLAGS = 0x10;
constexpr ULONG ENF_BACKGROUND = 1UL << 9;
// CScreenPolygon
constexpr size_t SPO_BRUSHPOLYGON = 0xC4;
constexpr size_t SPO_CTEDGEVX = 0xE4;                  // edge vertices after clipping (0: nothing visible)
constexpr size_t SPO_EDGE0 = 0xE8;                     // first screen edge
constexpr size_t SPO_CTEDGES = 0xEC;
// CScreenEdge
constexpr size_t SED_SCREENPOLYGON = 0x08;
// CBrushPolygon
constexpr size_t BPO_FLAGS = 0xB8;
constexpr ULONG BPOF_RENDERASPORTAL = 1UL << 6;
constexpr ULONG BPOF_DETAILPOLYGON = 1UL << 21;
constexpr ULONG BPOF_PORTAL = 1UL << 22;

// void RenderScene(CDrawPort *pdp, ScenePolygon *pspoFirst, FLOAT, FLOAT, FLOAT, COLOR)
typedef void (__cdecl *RenderScene_t)(void *pdp, void *pspoFirst, float f0, float f1, float f2, ULONG col);
// void CDrawPort::FillZBuffer(FLOAT zval) const (__thiscall)
typedef void (__fastcall *FillZBuffer_t)(void *pdp, void *pvEdx, float fZ);
// CRenderer methods (__thiscall)
typedef void (__fastcall *RendererVoid_t)(void *pre, void *pvEdx);
typedef void (__fastcall *RendererPtr_t)(void *pre, void *pvEdx, void *pv);

RenderScene_t _pfnRenderScene;
FillZBuffer_t _pfnFillZBuffer;
void **_ppGfx;                                        // _pGfx

template<class T> T &Field(void *p, size_t off) { return *(T *)((char *)p + off); }

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

// a detail polygon that is drawn whole instead of going through the span buffer
// (not in background brushes: the background is drawn without the depth test)
bool IsDrawnWhole(void *pre, void *pspo)
{
  if (pspo == nullptr || Field<int>(pre, RE_RENDERINGSHADOWS) != 0) return false;
  void *pbsc = Field<void *>(pre, RE_CURRENTSECTOR);
  void *pbm = pbsc != nullptr ? Field<void *>(pbsc, BSC_BRUSHMIP) : nullptr;
  void *pbr = pbm != nullptr ? Field<void *>(pbm, BM_BRUSH) : nullptr;
  void *pen = pbr != nullptr ? Field<void *>(pbr, BR_ENTITY) : nullptr;
  if (pen == nullptr || (Field<ULONG>(pen, EN_FLAGS) & ENF_BACKGROUND)) return false;
  void *pbpo = Field<void *>(pspo, SPO_BRUSHPOLYGON);
  if (pbpo == nullptr) return false;
  ULONG ulFlags = Field<ULONG>(pbpo, BPO_FLAGS);
  return (ulFlags & BPOF_DETAILPOLYGON) && !(ulFlags & (BPOF_PORTAL | BPOF_RENDERASPORTAL));
}

void __fastcall AddEdge_Hook(void *pre, void *pvEdx, void *psed)
{
  if (IsDrawnWhole(pre, Field<void *>(psed, SED_SCREENPOLYGON))) return;
  ((RendererPtr_t)ENG_ADDEDGE)(pre, pvEdx, psed);
}

// detail polygons of the sectors added so far, rasterized after the scan
// (the scan starts by resetting the rasterizer, and the viewer's own sectors
// are added before it)
struct DetailPolygon { void *pre, *pspo; };
std::vector<DetailPolygon> _adetails;

void __fastcall AddScreenEdges_Hook(void *pre, void *pvEdx)
{
  ((RendererVoid_t)ENG_ADDSCREENEDGES)(pre, pvEdx);
  void *pbsc = Field<void *>(pre, RE_CURRENTSECTOR);
  void **apspo = Field<void **>(pre, RE_SCREENPOLYGONS);
  int ispo0 = Field<int>(pbsc, BSC_ISPO0);
  int ctspo = Field<int>(pbsc, BSC_CTSPO);
  for (int ispo = ispo0; ispo < ispo0 + ctspo; ispo++) {
    void *pspo = apspo[ispo];
    // AddScreenEdges() sets the edge range only for a polygon with edge
    // vertices; screen polygons are reused, so otherwise it is last frame's
    if (IsDrawnWhole(pre, pspo) && Field<int>(pspo, SPO_CTEDGEVX) > 0 && Field<int>(pspo, SPO_CTEDGES) > 0) {
      _adetails.push_back({pre, pspo});
    }
  }
}

// start of a view: forget what an earlier view left (one that ended before its scan)
void __fastcall InitRendering_Hook(void *pre, void *pvEdx)
{
  _adetails.clear();
  ((RendererVoid_t)ENG_INIT)(pre, pvEdx);
}

// CRenderer::ScanEdges() and what follows it (0x60096350), then the detail
// polygons, each over its whole area
void __fastcall Scan_Hook(void *pre, void *pvEdx)
{
  ((RendererVoid_t)ENG_SCAN)(pre, pvEdx);
  int &iScanLine = Field<int>(pre, RE_CURRENTSCANLINE);
  int iScanLineOld = iScanLine;
  iScanLine = Field<int>(pre, RE_TOPSCANLINE);
  void **apsed = *(void ***)ENG_SCREENEDGES;
  std::vector<DetailPolygon> adetails;
  adetails.swap(_adetails);
  for (const DetailPolygon &dp : adetails) {
    if (dp.pre != pre) continue;   // left from a view that wasn't scanned
    int ised0 = Field<int>(dp.pspo, SPO_EDGE0);
    int ctsed = Field<int>(dp.pspo, SPO_CTEDGES);
    if (ised0 < 0 || ctsed <= 0 || ised0 + ctsed > *(int *)ENG_CTSCREENEDGES) continue;
    (*(int *)ENG_RASTER_DEPTH)++;
    ((RendererPtr_t)ENG_RASTER_BEGIN)(pre, nullptr, dp.pspo);
    for (int ised = ised0; ised < ised0 + ctsed; ised++) {
      ((RendererPtr_t)ENG_RASTER_EDGE)(pre, nullptr, apsed[ised]);
    }
    ((RendererPtr_t)ENG_RASTER_END)(pre, nullptr, dp.pspo);
    (*(int *)ENG_RASTER_DEPTH)--;
  }
  iScanLine = iScanLineOld;
}

// a call instruction at addr to target
void MakeCall(BYTE (&aub)[5], uintptr_t addr, uintptr_t target)
{
  aub[0] = 0xE8;
  LONG slRel = (LONG)(target - (addr + 5));
  memcpy(aub + 1, &slRel, 4);
}

bool RedirectCall(uintptr_t addr, uintptr_t original, void *pNew)
{
  BYTE aub[5];
  MakeCall(aub, addr, original);
  if (!CodeMatches(addr, aub, sizeof(aub))) return false;
  MakeCall(aub, addr, (uintptr_t)pNew);
  return WriteCode((void *)addr, aub, sizeof(aub));
}

}  // namespace

void InstallZOrderFix()
{
  if (!g_cfg.bZOrderFix) return;
  _pfnRenderScene = (RenderScene_t)EngineProc("?RenderScene@@YAXPAVCDrawPort@@PAUScenePolygon@@MMMK@Z");
  _pfnFillZBuffer = (FillZBuffer_t)EngineProc("?FillZBuffer@CDrawPort@@QBEXM@Z");
  _ppGfx = (void **)EngineProc("?_pGfx@@3PAVCGfxLibrary@@A");
  // mov eax, [_pGfx]; cmp [eax + 0x84], esi
  static const BYTE aubFlagTest[] = { 0xA1, 0x04, 0xD6, 0x10, 0x60, 0x39, 0xB0, 0x84, 0x00, 0x00, 0x00 };
  // PassPortal: test [eax + 0xB8], BPOF_RENDERTRANSLUCENT; ...; push esi; call rasterizer begin
  static const BYTE aubPassPortal[] = { 0xF7, 0x80, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00 };
  // AddScreenEdges: mov edx, [ebp - 0x28]; push esi; mov ecx, edi; mov [esi + 8], edx (the edge's polygon)
  static const BYTE aubEdgePolygon[] = { 0x8B, 0x55, 0xD8, 0x56, 0x8B, 0xCF, 0x89, 0x56, 0x08 };
  bool bOk = _pfnRenderScene == (RenderScene_t)ENG_RENDERSCENE && _pfnFillZBuffer != nullptr && _ppGfx != nullptr
          && CodeMatches(ENG_DEPTHFLAG_TEST, aubFlagTest, sizeof(aubFlagTest))
          && CodeMatches(0x600919AD, aubPassPortal, sizeof(aubPassPortal))
          && CodeMatches(ENG_ADDEDGE_CALL - sizeof(aubEdgePolygon), aubEdgePolygon, sizeof(aubEdgePolygon))
          && RedirectCall(ENG_MAINPASS_CALL, ENG_RENDERSCENE, (void *)RenderMainScene)
          && RedirectCall(ENG_ADDEDGE_CALL, ENG_ADDEDGE, (void *)AddEdge_Hook)
          && RedirectCall(ENG_ADDSCREENEDGES_CALL, ENG_ADDSCREENEDGES, (void *)AddScreenEdges_Hook)
          && RedirectCall(ENG_SCAN_CALL, ENG_SCAN, (void *)Scan_Hook)
          && RedirectCall(ENG_INIT_CALL, ENG_INIT, (void *)InitRendering_Hook);
  Log("Z-order fix: %s", bOk ? "installed" : "partly failed");
}
