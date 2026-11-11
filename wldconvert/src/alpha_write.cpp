// Writing a world in the alpha's format (Engine.dll of 13 Jan 2000):
//  - no engine build header and no world info chunk (WLIF); the description is
//    the first thing in the world state
//  - brush mips store the old mip factor instead of a distance (no BRMP)
//  - sectors are version 1 (name, colors, flags), polygons version 3 (no
//    triangulation)
//  - entities have no ID, size, spawn flags or rotation matrix in their
//    header, and are listed in an ENTs chunk; pointers are entity indices
//  - each class level writes its own data after the properties: health
//    (CLiveEntity), timer and state stack (CRationalEntity), the "in movers"
//    flag (CMovableEntity).
#include "convert.h"

#include <cstdio>

namespace {

void WritePlacement(Writer &w, const Placement &pl) { w.Raw(pl.af, sizeof(pl.af)); }

float DistanceToMipFactor(float fDistance)
{
  if (fDistance >= 1E6f * 0.999f) return 100.0f;
  float fFactor = log2f(fDistance * 1024.0f / 320.0f);
  return fFactor;
}

void WriteBrushes(Writer &w, const World &wo)
{
  w.ID("BRAR");
  w.I32((int32_t)wo.abrushes.size());
  for (const Brush &br : wo.abrushes) {
    w.ID("BR3D");
    w.I32(14);
    w.I32((int32_t)br.amips.size());
    for (const BrushMip &bm : br.amips) {
      w.F32(DistanceToMipFactor(bm.fMaxDistance));
      w.I32((int32_t)bm.asectors.size());
      for (const Sector &sc : bm.asectors) {
        w.ID("BSC ");
        w.I32(1);
        w.String(sc.strName);
        w.U32(sc.colColor);
        w.U32(sc.colAmbient);
        w.U32(sc.ulFlags);
        w.ID("VTXs"); w.I32(sc.ctVertices); w.Raw(sc.abVertices);
        w.ID("PLNs"); w.I32(sc.ctPlanes);   w.Raw(sc.abPlanes);
        w.ID("EDGs"); w.I32(sc.ctEdges);    w.Raw(sc.abEdges);
        w.ID("BPOs");
        w.I32(3);
        w.I32((int32_t)sc.apolygons.size());
        for (const BrushPolygon &po : sc.apolygons) {
          w.I32(po.iPlane);
          w.U32(po.colColor);
          w.U32(po.ulFlags);
          for (const Texture &tex : po.atex) {
            w.FileName(tex.strFile);
            w.Raw(tex.aubMapping, sizeof(tex.aubMapping));
            w.U8(tex.ubScroll);
            w.U8(tex.ubBlend);
            w.U8(tex.ubFlags);
            w.U8(tex.ubDummy);
            w.U32(tex.colColor);
          }
          w.Raw(po.aubProperties, sizeof(po.aubProperties));
          w.I32((int32_t)po.aiEdges.size());
          if (!po.aiEdges.empty()) w.Raw(po.aiEdges.data(), po.aiEdges.size() * 4);
          w.ID("LSHM");
          w.Raw(po.aubShadowMap, sizeof(po.aubShadowMap));
          w.ID("SHAL");
          w.I32(po.iShadowVersion);
          w.I32(po.slPolygonSizeU);
          w.I32(po.slPolygonSizeV);
          w.I32((int32_t)po.alayers.size());
          for (const ShadowLayer &sl : po.alayers) {
            w.U32(sl.ulFlags);
            w.I32(sl.abMask.empty() ? 0 : sl.slSizeInPixels);
            w.Raw(sl.abMask);
            w.Raw(sl.aiRect, sizeof(sl.aiRect));
          }
          w.U32(po.colShadow);
        }
        if (sc.bHasBSP) {
          w.ID("BSP0");
          w.I32(sc.iBSPVersion);
          w.I32((int32_t)sc.abBSP.size());
          w.Raw(sc.abBSP);
          w.ID("BSPE");
        }
      }
    }
    w.ID("BREN");
  }
  if (wo.bHasPortalLinks) {
    w.ID("PSLS");
    w.I32(wo.iPortalLinksVersion);
    w.I32((int32_t)wo.abPortalLinks.size());
    w.Raw(wo.abPortalLinks);
    w.ID("PSLE");
  }
  w.ID("EOAR");
}

void WriteProperty(Writer &w, const OutProperty &op)
{
  w.U32((op.ulID << 8) | (uint32_t)op.ept);
  switch (op.ept) {
  case EPT_BOOL: case EPT_INDEX: case EPT_ENUM: case EPT_ANIMATION:
  case EPT_ILLUMINATIONTYPE: case EPT_COLOR: case EPT_ANGLE: case EPT_ENTITYPTR:
    w.I32(op.iValue); break;
  case EPT_FLOAT: case EPT_RANGE:
    w.F32(op.fValue); break;
  case EPT_STRING: case EPT_FILENAMENODEP:
    w.String(op.strValue); break;
  case EPT_FILENAME:
    w.FileName(op.strValue); break;
  default:
    w.Raw(op.abValue); break;
  }
}

bool IsLive(const std::string &str)     { return str != "CEntity"; }
bool IsRational(const std::string &str) { return str != "CEntity" && str != "CLiveEntity"; }
bool IsMovable(const std::string &str)
{
  return str == "CMovableEntity" || str == "CMovableModelEntity" || str == "CMovableBrushEntity";
}

void WriteEntity(Writer &w, const OutEntity &oe)
{
  w.U32(oe.ulRenderType);
  w.U32(oe.ulPhysicsFlags);
  w.U32(oe.ulCollisionFlags);
  w.U32(oe.ulFlags);
  if (oe.ulRenderType == RT_BRUSH || oe.ulRenderType == RT_FIELDBRUSH) w.I32(oe.iBrush);
  if (oe.bHasParent) {
    w.ID("PART");
    w.I32(oe.iParent);
    WritePlacement(w, oe.plRelative);
  }
  w.ID("PRPS");
  w.I32((int32_t)oe.aprops.size());
  for (const OutProperty &op : oe.aprops) WriteProperty(w, op);
  if (oe.bHasLight) {
    w.ID("LIGH");
    w.I32((int32_t)oe.alayers.size());
    for (const LightLayerRef &ll : oe.alayers) {
      w.I32(ll.iBrush);
      w.I32(ll.iMip);
      w.I32(ll.iSector);
      w.I32(ll.iPolygon);
      w.I32(ll.iLayer);
    }
  }
  const std::string &strBase = oe.pac->strEngineBase;
  if (IsLive(strBase)) w.F32(oe.fHealth);
  if (IsRational(strBase)) {
    w.F32(-1.0f);   // THINKTIME_NEVER: no timer pending
    w.I32(0);       // empty state stack: the entity waits for its initialization
  }
  if (IsMovable(strBase)) w.U32(0);   // not in the movers list
}

}  // namespace

Bytes WriteAlphaWorld(const OutWorld &out)
{
  const World &wo = *out.pwoSource;
  Writer w;
  w.ID("WRLD");

  w.BeginDictionary();
  WriteBrushes(w, wo);
  w.EndDictionary();

  w.BeginDictionary();
  w.ID("WSTA");
  w.I32(9);
  w.String(wo.strDescription);
  w.U32(wo.colBackground);
  w.ID("BGVW");
  w.I32(out.iBackgroundViewer);
  w.ID("BRDP");
  for (const std::string &str : wo.astrBackdrop) w.String(str);
  w.Raw(wo.afBackdrop, sizeof(wo.afBackdrop));
  w.ID("BDRO");
  w.String(wo.strBackdropObject);
  w.ID("VWPS");
  WritePlacement(w, wo.plViewer);
  w.F32(wo.fViewerDistance);
  if (wo.bHasThumbnail) {
    w.ID("TBPS");
    WritePlacement(w, wo.plThumbnail);
    w.F32(wo.fThumbnailDistance);
  }
  w.ID("ENTs");
  w.I32((int32_t)out.aentities.size());
  for (const OutEntity &oe : out.aentities) {
    w.FileName(oe.pac->strFile);
    WritePlacement(w, oe.pl);
  }
  for (const OutEntity &oe : out.aentities) WriteEntity(w, oe);
  w.EndDictionary();
  w.ID("WEND");
  return w.Data();
}

std::string SpawnFlagsText(const std::string &strWorld, int32_t ctEntities, const std::vector<SpawnFlagsEntry> &aEntries)
{
  if (aEntries.empty()) return std::string();
  std::string str = "AlphaFixes spawn flags 1\n";
  str += "; The spawn flags of " + strWorld + ", which the alpha's level format can't hold.\n";
  str += "; Written by WldConvert from the First Encounter level; AlphaFixes reads it when the level is loaded.\n";
  str += "; Flags: Easy 1, Normal 2, Hard 4, Extreme 8, Single 10000, Deathmatch 20000, Cooperative 40000.\n";
  str += "; entity index, class, position x y z (to check the entity), flags (hex)\n";
  str += "Entities " + std::to_string(ctEntities) + "\n";
  for (const SpawnFlagsEntry &se : aEntries) {
    char ach[512];
    snprintf(ach, sizeof(ach), "%d \"%s\" %.9g %.9g %.9g %08X\n", se.iEntity, se.strClass.c_str(), se.af[0], se.af[1],
             se.af[2], se.ulFlags);
    str += ach;
  }
  return str;
}
