// Touch fields that never fire.
//
// A touch field made in the alpha's World Editor with the primitive's Room
// option off never fires, and the console says "Warning: Open sector in a
// field brush!".  Fields converted from The First Encounter do not fire
// either.  Two things in the engine stop them:
//
// 1. The field's BSP tree is inside out.  Fields are found through the field
//    sector's BSP tree: every tick a moving entity tests its collision spheres
//    against the trees of the field sectors it is in (Engine.dll 0x600B58D0),
//    and only a sphere inside a tree gets an EField sent to the field.
//    CBrushSector::CalculateBoundingBoxes() builds the tree straight from the
//    polygons' planes.  With Room off (and in TFE's fields) the polygons face
//    outwards and CBrushSector::CalculateVolume() calls the sector open; its
//    tree's "inside" is everything around the field and the field's own box
//    is "outside".  The tree is saved with the level and read back as it is.
//    The alpha's own levels only have closed (inward, Room on) field sectors.
//
// 2. A moving entity only looks for new sectors through portals.  Each move
//    CEntity::FindSectorsAroundEntityNear() tests the sectors the entity is in
//    and the sectors of the polygons near it, which reach further only through
//    portal polygons linked to the sectors behind them.  A field is found only
//    if a portal crosses it, as in every field of the alpha's levels.  A field
//    inside one room (or TFE's fields, made for an engine that finds them
//    anyway) is never found; only the full search when an entity spawns or
//    teleports looks at every sector.
//
// The fixes:
//  - After a field sector's tree is built or read, a point far outside the
//    sector is tested against it.  A tree that has that point inside is inside
//    out, and its inside and outside leaves are swapped, so the tree's inside
//    is the field's box whichever way the polygons face.  Testing the tree
//    rather than the open flag leaves a tree that is already right alone.
//  - After FindSectorsAroundEntityNear() the entity also gets the field
//    sectors it is in, tested like FindSectorsAroundEntity() does (bounding
//    box, then the tree).  The next move's search drops them again when the
//    entity leaves, like any other sector.
//  - The open sector warning, which no longer means a broken field, is not
//    printed.
#include "alphafixes.h"

#include <vector>

namespace {

// CEntity
constexpr size_t EN_RENDERTYPE = 0x04;
constexpr size_t EN_FLAGS = 0x10;
constexpr size_t EN_POSITION = 0x1C;          // en_plPlacement.pl_PositionVector
constexpr size_t EN_WORLD = 0x70;
constexpr size_t EN_SECTORS = 0x74;           // en_rdSectors (CRelationDst)
constexpr size_t EN_SPATIALRADIUS = 0x68;     // radius FindSectorsAroundEntity() uses; < 0: none
constexpr int RT_BRUSH = 4;
constexpr int RT_FIELDBRUSH = 8;
constexpr ULONG ENF_ZONING = 0x02;
// CWorld, CBrushArchive, CBrush3D, CBrushMip
constexpr size_t WO_BRUSHES = 0x44;           // CBrushArchive *
constexpr size_t BA_BRUSHES_COUNT = 0x14 + 0x10;   // ba_abrBrushes: count, pointers
constexpr size_t BA_BRUSHES = 0x14 + 0x0C;
constexpr size_t BR_ENTITY = 0x660;
constexpr size_t BR_FIELDSETTINGS = 0x664;    // set for field brushes (what CalculateVolume() checks)
constexpr size_t BR_MIPS = 0x66C;             // list of mips; the node is at +0x1C in the mip
constexpr size_t BM_NODE = 0x1C;
constexpr size_t BM_SECTORS = 0x0C;           // pointers, then count
constexpr size_t BM_SECTORS_COUNT = 0x10;
constexpr size_t BM_BRUSH = 0x18;
// CBrushSector
constexpr size_t BSC_BRUSHMIP = 0x38;
constexpr size_t BSC_BOX = 0x4C;              // bounding box: min, then max (floats)
constexpr size_t BSC_BSPTREE = 0x6C;          // BSPTree<DOUBLE, 3> *
constexpr size_t BSC_ENTITIES = 0x7C;         // bsc_rsEntities (CRelationSrc)
// CRelationLnk: its node in the destination's list is at +0x10
constexpr size_t LNK_DSTNODE = 0x10;
// BSPTree<DOUBLE, 3>: node count at +0, root at +8; BSPNode: plane, then location, front, back
constexpr size_t BT_COUNT = 0x00;
constexpr size_t BT_ROOT = 0x08;
constexpr size_t BN_LOCATION = 0x20;
constexpr size_t BN_FRONT = 0x24;
constexpr size_t BN_BACK = 0x28;
constexpr int BNL_INSIDE = 1;
constexpr int BNL_OUTSIDE = 2;

// Engine.dll (alpha build)
constexpr uintptr_t ENG_BSPTREE_TESTSPHERE = 0x600798B0;   // BSPTree<DOUBLE, 3>::TestSphere(const DOUBLE3D &, DOUBLE): 0 outside
constexpr uintptr_t ENG_OPENFIELD_WARNING = 0x600AC7FA;    // CalculateVolume(): je over the warning

using CalculateBoundingBoxes_t = void(__fastcall *)(char *pbsc, void *pvEdx, void *ppr);
using SectorRead_t = void(__fastcall *)(char *pbsc, void *pvEdx, void *pstrm);
using FindSectorsNear_t = void(__fastcall *)(char *pen, void *pvEdx);
using TestSphere_t = int(__fastcall *)(char *pbt, void *pvEdx, const double *pvCenter, double dRadius);
using GetSrc_t = char *(__fastcall *)(char *plnk, void *pvEdx);
using AddRelationPair_t = void(__cdecl *)(char *prsSrc, char *prdDst);

CalculateBoundingBoxes_t _pfnCalculateBoundingBoxes = nullptr;
SectorRead_t _pfnSectorRead = nullptr;
FindSectorsNear_t _pfnFindSectorsNear = nullptr;
TestSphere_t _pfnTestSphere = (TestSphere_t)ENG_BSPTREE_TESTSPHERE;
GetSrc_t _pfnGetSrc = nullptr;
AddRelationPair_t _pfnAddHeadHead = nullptr;
AddRelationPair_t _pfnAddTailTail = nullptr;
long _ctTurned = 0;

bool HasTree(char *pbt)
{
  return pbt != nullptr && *(int *)(pbt + BT_COUNT) > 0 && *(char **)(pbt + BT_ROOT) != nullptr;
}

void FixFieldTree(char *pbsc, const char *strWhen)
{
  char *pbm = *(char **)(pbsc + BSC_BRUSHMIP);
  char *pbr = pbm ? *(char **)(pbm + BM_BRUSH) : nullptr;
  char *pbt = *(char **)(pbsc + BSC_BSPTREE);
  if (pbr == nullptr || *(void **)(pbr + BR_FIELDSETTINGS) == nullptr || !HasTree(pbt)) return;
  static const double avFar[3] = { 1.23e6, 2.34e6, 3.45e6 };
  if (_pfnTestSphere(pbt, nullptr, avFar, 0.0) == 0) return;
  std::vector<char *> apbn{ *(char **)(pbt + BT_ROOT) };
  while (!apbn.empty()) {
    char *pbn = apbn.back();
    apbn.pop_back();
    if (pbn == nullptr) continue;
    int &iLocation = *(int *)(pbn + BN_LOCATION);
    if (iLocation == BNL_INSIDE || iLocation == BNL_OUTSIDE) {
      iLocation = BNL_INSIDE + BNL_OUTSIDE - iLocation;
    } else {
      apbn.push_back(*(char **)(pbn + BN_FRONT));
      apbn.push_back(*(char **)(pbn + BN_BACK));
    }
  }
  if (_ctTurned++ < 10) Log("Field brush: sector %p's BSP tree was inside out (%s); turned", (void *)pbsc, strWhen);
}

bool IsInSector(char *pen, char *pbsc)
{
  for (char *pnd = *(char **)(pen + EN_SECTORS); *(char **)pnd != nullptr; pnd = *(char **)pnd) {
    if (_pfnGetSrc(pnd - LNK_DSTNODE, nullptr) - BSC_ENTITIES == pbsc) return true;
  }
  return false;
}

void AddFieldSectors(char *pen)
{
  char *pwo = *(char **)(pen + EN_WORLD);
  float fRadius = *(float *)(pen + EN_SPATIALRADIUS);
  if (pwo == nullptr || fRadius < 0.0f) return;
  char *pba = *(char **)(pwo + WO_BRUSHES);
  if (pba == nullptr) return;
  const float *pfPos = (const float *)(pen + EN_POSITION);
  const double avCenter[3] = { pfPos[0], pfPos[1], pfPos[2] };
  int ctBrushes = *(int *)(pba + BA_BRUSHES_COUNT);
  char **apbr = *(char ***)(pba + BA_BRUSHES);
  for (int iBrush = 0; iBrush < ctBrushes; iBrush++) {
    char *pbr = apbr[iBrush];
    char *penField = *(char **)(pbr + BR_ENTITY);
    if (penField == nullptr || penField == pen || *(int *)(penField + EN_RENDERTYPE) != RT_FIELDBRUSH
     || !(*(ULONG *)(penField + EN_FLAGS) & ENF_ZONING)) continue;
    char *pnd = *(char **)(pbr + BR_MIPS);   // the first mip, as FindSectorsAroundEntity()
    if (pnd == nullptr || *(char **)pnd == nullptr) continue;
    char *pbm = pnd - BM_NODE;
    int ctSectors = *(int *)(pbm + BM_SECTORS_COUNT);
    for (int iSector = 0; iSector < ctSectors; iSector++) {
      char *pbsc = (*(char ***)(pbm + BM_SECTORS))[iSector];
      const float *pfBox = (const float *)(pbsc + BSC_BOX);
      bool bNear = true;
      for (int i = 0; i < 3; i++) bNear &= pfPos[i] + fRadius >= pfBox[i] && pfPos[i] - fRadius <= pfBox[3 + i];
      char *pbt = *(char **)(pbsc + BSC_BSPTREE);
      if (!bNear || !HasTree(pbt) || _pfnTestSphere(pbt, nullptr, avCenter, fRadius) == 0 || IsInSector(pen, pbsc)) continue;
      // as FindSectorsAroundEntity(): brushes at the head of the sector's list, other entities at the tail
      if (*(int *)(pen + EN_RENDERTYPE) == RT_BRUSH) {
        _pfnAddHeadHead(pbsc + BSC_ENTITIES, pen + EN_SECTORS);
      } else {
        _pfnAddTailTail(pbsc + BSC_ENTITIES, pen + EN_SECTORS);
      }
    }
  }
}

void __fastcall CalculateBoundingBoxes_Hook(char *pbsc, void *pvEdx, void *ppr)
{
  _pfnCalculateBoundingBoxes(pbsc, pvEdx, ppr);
  FixFieldTree(pbsc, "built");
}

void __fastcall SectorRead_Hook(char *pbsc, void *pvEdx, void *pstrm)
{
  _pfnSectorRead(pbsc, pvEdx, pstrm);
  FixFieldTree(pbsc, "read");
}

void __fastcall FindSectorsNear_Hook(char *pen, void *pvEdx)
{
  _pfnFindSectorsNear(pen, pvEdx);
  AddFieldSectors(pen);
}

}  // namespace

void InstallFieldBrushFix()
{
  if (!g_cfg.bFieldBrushFix) return;
  _pfnGetSrc = (GetSrc_t)EngineProc("?GetSrc@CRelationLnk@@QAEAAVCRelationSrc@@XZ");
  _pfnAddHeadHead = (AddRelationPair_t)EngineProc("?AddRelationPairHeadHead@@YAXAAVCRelationSrc@@AAVCRelationDst@@@Z");
  _pfnAddTailTail = (AddRelationPair_t)EngineProc("?AddRelationPairTailTail@@YAXAAVCRelationSrc@@AAVCRelationDst@@@Z");
  // push ebp; mov ebp, esp; push ebx; push esi; mov esi, [ecx + 8]; push edi; mov eax, [esi + 0x20]
  static const BYTE aubTestSphere[] = { 0x55, 0x8B, 0xEC, 0x53, 0x56, 0x8B, 0x71, 0x08, 0x57, 0x8B, 0x46, 0x20 };
  // je over the warning; push the warning text
  static const BYTE aubWarning[] = { 0x74, 0x2B, 0x68, 0x64, 0x42, 0x10, 0x60 };
  bool bOk = _pfnGetSrc != nullptr && _pfnAddHeadHead != nullptr && _pfnAddTailTail != nullptr
          && CodeMatches(ENG_BSPTREE_TESTSPHERE, aubTestSphere, sizeof(aubTestSphere))
          && CodeMatches(ENG_OPENFIELD_WARNING, aubWarning, sizeof(aubWarning))
          && HookFunction(EngineProc("?CalculateBoundingBoxes@CBrushSector@@QAEXAAVCSimpleProjection3D_DOUBLE@@@Z"),
                          (void *)CalculateBoundingBoxes_Hook, (void **)&_pfnCalculateBoundingBoxes,
                          "CBrushSector::CalculateBoundingBoxes")
          && HookFunction(EngineProc("?Read_t@CBrushSector@@QAEXPAVCTStream@@@Z"), (void *)SectorRead_Hook,
                          (void **)&_pfnSectorRead, "CBrushSector::Read_t")
          && HookFunction(EngineProc("?FindSectorsAroundEntityNear@CEntity@@QAEXXZ"), (void *)FindSectorsNear_Hook,
                          (void **)&_pfnFindSectorsNear, "CEntity::FindSectorsAroundEntityNear");
  if (bOk) {
    static const BYTE ubJmp = 0xEB;
    WriteCode((void *)ENG_OPENFIELD_WARNING, &ubJmp, 1);
  }
  Log("Field brush fix: %s", bOk ? "installed" : "partly failed");
}
