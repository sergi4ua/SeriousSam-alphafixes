// Reading a Serious Sam: The First Encounter world (engine build 10000).
// The layout follows CWorld::Load_t in Croteam's Serious Engine 1 sources.
#include "convert.h"

namespace {

void ReadPlacement(Reader &r, Placement &pl) { r.Raw(pl.af, sizeof(pl.af)); }

// --- brushes --------------------------------------------------------------------

void ReadTexture(Reader &r, Texture &tex)
{
  tex.strFile = r.FileName();
  r.Raw(tex.aubMapping, sizeof(tex.aubMapping));
  tex.ubScroll = r.U8();
  tex.ubBlend = r.U8();
  tex.ubFlags = r.U8();
  tex.ubDummy = r.U8();
  tex.colColor = r.U32();
}

void ReadShadowMap(Reader &r, BrushPolygon &po)
{
  if (r.IsID("CTSM")) r.Fail("shadow maps in the old CTSM format are not supported");
  r.ExpectID("LSHM");
  r.Raw(po.aubShadowMap, sizeof(po.aubShadowMap));
  if (!r.IsID("SHAL")) r.Fail("unsupported shadow layer format '" + Reader::Printable(r.PeekID()) + "'");
  r.ExpectID("SHAL");
  po.iShadowVersion = r.I32();
  po.slPolygonSizeU = r.I32();
  po.slPolygonSizeV = r.I32();
  int32_t ctLayers = r.Count(24);
  po.alayers.resize(ctLayers);
  for (ShadowLayer &sl : po.alayers) {
    sl.ulFlags = r.U32();
    sl.slSizeInPixels = r.I32();
    if (sl.slSizeInPixels < 0) r.Fail("bad shadow layer size");
    if (sl.slSizeInPixels != 0) sl.abMask = r.Raw(((size_t)sl.slSizeInPixels + 7) / 8);
    r.Raw(sl.aiRect, sizeof(sl.aiRect));
  }
}

void ReadSector(Reader &r, Sector &sc)
{
  int32_t iVersion = 0;
  if (r.OptionalID("BSC ")) iVersion = r.I32();
  if (iVersion >= 1) sc.strName = r.String();
  sc.colColor = r.U32();
  sc.colAmbient = r.U32();
  sc.ulFlags = r.U32();
  if (iVersion >= 2) r.U32();   // flags 2 (visibility include/exclude): no such thing in the alpha
  if (iVersion >= 3) r.U32();   // visibility flags: same
  r.ExpectID("VTXs");
  sc.ctVertices = r.Count(24);
  sc.abVertices = r.Raw((size_t)sc.ctVertices * 24);
  r.ExpectID("PLNs");
  sc.ctPlanes = r.Count(32);
  sc.abPlanes = r.Raw((size_t)sc.ctPlanes * 32);
  r.ExpectID("EDGs");
  sc.ctEdges = r.Count(8);
  sc.abEdges = r.Raw((size_t)sc.ctEdges * 8);

  r.ExpectID("BPOs");
  int32_t iPolyVersion = r.I32();
  if (iPolyVersion < 2) r.Fail("brush polygon version " + std::to_string(iPolyVersion) + " is too old");
  int32_t ctPolygons = r.Count(100);
  sc.apolygons.resize(ctPolygons);
  for (BrushPolygon &po : sc.apolygons) {
    po.iPlane = r.I32();
    po.colColor = r.U32();
    po.ulFlags = r.U32();
    for (Texture &tex : po.atex) ReadTexture(r, tex);
    r.Raw(po.aubProperties, sizeof(po.aubProperties));
    int32_t ctEdges = r.Count(4);
    po.aiEdges.resize(ctEdges);
    if (ctEdges > 0) r.Raw(po.aiEdges.data(), ctEdges * 4);
    if (iPolyVersion >= 4) {
      // triangulation (not in the alpha's format; the alpha makes its own)
      int32_t ct = r.Count(4);
      r.Skip((size_t)ct * 4);
      ct = r.Count(4);
      r.Skip((size_t)ct * 4);
    }
    ReadShadowMap(r, po);
    po.colShadow = r.U32();
  }
  if (r.OptionalID("BSP0")) {
    sc.bHasBSP = true;
    sc.iBSPVersion = r.I32();
    int32_t slSize = r.Count(1);
    sc.abBSP = r.Raw(slSize);
    r.ExpectID("BSPE");
  }
}

void ReadBrushes(Reader &r, World &wo)
{
  r.ExpectID("BRAR");
  int32_t ctBrushes = r.Count(12);
  wo.abrushes.resize(ctBrushes);
  for (Brush &br : wo.abrushes) {
    r.ExpectID("BR3D");
    int32_t iVersion = r.I32();
    if (iVersion != 14) r.Fail("brush version " + std::to_string(iVersion) + " is not supported (expected 14)");
    int32_t ctMips = r.Count(8);
    br.amips.resize(ctMips);
    for (BrushMip &bm : br.amips) {
      if (r.OptionalID("BRMP")) {
        bm.fMaxDistance = r.F32();
      } else {
        bm.fMaxDistance = MipFactorToDistance(r.F32());
      }
      int32_t ctSectors = r.Count(16);
      bm.asectors.resize(ctSectors);
      for (Sector &sc : bm.asectors) ReadSector(r, sc);
    }
    r.ExpectID("BREN");
  }
  if (r.OptionalID("PSLS")) {
    wo.bHasPortalLinks = true;
    wo.iPortalLinksVersion = r.I32();
    int32_t slSize = r.Count(1);
    wo.abPortalLinks = r.Raw(slSize);
    r.ExpectID("PSLE");
  }
  r.ExpectID("EOAR");
  if (r.IsID("TRAR")) r.Fail("terrains are not supported");
}

// --- entities -------------------------------------------------------------------

void SkipAnimObject(Reader &r)
{
  if (r.OptionalID("ANOB")) r.Skip(16);   // start time, current anim, last anim, flags
  else r.Skip(8);
}

void SkipTextureObject(Reader &r)
{
  r.FileName();
  SkipAnimObject(r);
}

// ReadModelObject_t: returns the model file name
std::string SkipModelObject(Reader &r)
{
  std::string strModel = r.FileName();
  SkipAnimObject(r);
  if (r.OptionalID("MODT")) r.Skip(4);    // blend color
  r.Skip(4 + 12 + 4);                       // patch mask, stretch, color mask
  if (r.OptionalID("MTEX")) {
    for (int i = 0; i < 4; i++) SkipTextureObject(r);
  } else {
    SkipTextureObject(r);
  }
  if (r.OptionalID("ATCH")) {
    int32_t ct = r.Count(28);
    for (int32_t i = 0; i < ct; i++) {
      r.Skip(4 + 24);                       // position, placement
      SkipModelObject(r);
    }
  }
  return strModel;
}

void ReadProperty(Reader &r, Property &p)
{
  uint32_t ulIDAndType = r.U32();
  p.ulID = ulIDAndType >> 8;
  p.ept = (PropertyType)(ulIDAndType & 0xFF);
  switch (p.ept) {
  case EPT_BOOL: case EPT_INDEX: case EPT_ENUM: case EPT_FLAGS: case EPT_ANIMATION:
  case EPT_ILLUMINATIONTYPE: case EPT_COLOR: case EPT_ANGLE: case EPT_ENTITYPTR:
    p.iValue = r.I32(); break;
  case EPT_FLOAT: case EPT_RANGE:
    p.fValue = r.F32(); break;
  case EPT_STRING: case EPT_FILENAMENODEP:
    p.strValue = r.String(); break;
  case EPT_STRINGTRANS:
    r.ExpectID("DTRS");
    p.strValue = r.String(); break;
  case EPT_FILENAME:
    p.strValue = r.FileName(); break;
  case EPT_FLOATAABBOX3D: p.abValue = r.Raw(24); break;
  case EPT_FLOATMATRIX3D: p.abValue = r.Raw(36); break;
  case EPT_FLOATQUAT3D:   p.abValue = r.Raw(16); break;
  case EPT_FLOAT3D:       p.abValue = r.Raw(12); break;
  case EPT_ANGLE3D:       p.abValue = r.Raw(12); break;
  case EPT_FLOATplane3D:  p.abValue = r.Raw(16); break;
  case EPT_PLACEMENT3D:   p.abValue = r.Raw(24); break;
  // runtime objects: read past them, they are not converted
  case EPT_MODELOBJECT:  SkipModelObject(r); break;
  case EPT_ANIMOBJECT:   r.FileName(); SkipAnimObject(r); break;
  case EPT_SOUNDOBJECT:  r.FileName(); r.Skip(80); break;
  default:
    r.Fail("unsupported property type " + std::to_string(p.ept));
  }
}

bool IsLiveBase(const std::string &str)
{
  return str != "CEntity";
}

// CEntity::Read_t (ENT4 chunk) up to the light source and health
void ReadEntityBody(Reader &r, const TfeClasses &tfe, Entity &en, size_t posEnd)
{
  en.ulRenderType = r.U32();
  en.ulPhysicsFlags = r.U32();
  en.ulCollisionFlags = r.U32();
  en.ulSpawnFlags = r.U32();
  en.ulFlags = r.U32();
  r.Skip(36);                               // rotation matrix (recomputed from the placement)
  switch (en.ulRenderType) {
  case RT_BRUSH: case RT_FIELDBRUSH:
    en.iBrush = r.I32(); break;
  case RT_MODEL: case RT_EDITORMODEL:
    en.strModel = SkipModelObject(r); break;
  case RT_TERRAIN: case RT_SKAMODEL: case RT_SKAEDITORMODEL:
    r.Fail("render type " + std::to_string(en.ulRenderType) + " is not supported");
  default:
    break;
  }
  if (r.OptionalID("PART")) {
    en.bHasParent = true;
    en.ulParent = r.U32();
    ReadPlacement(r, en.plRelative);
  }
  r.ExpectID("PRPS");
  int32_t ctProps = r.Count(8);
  en.aprops.resize(ctProps);
  for (Property &p : en.aprops) ReadProperty(r, p);
  if (r.OptionalID("LIGH")) {
    en.bHasLight = true;
    int32_t ctLayers = r.Count(20);
    en.alayers.resize(ctLayers);
    for (LightLayerRef &ll : en.alayers) {
      ll.iBrush = r.I32();
      ll.iMip = r.I32();
      ll.iSector = r.I32();
      ll.iPolygon = r.I32();
      ll.iLayer = r.I32();
    }
  }
  const TfeClass *ptc = tfe.ForFile(en.strClass);
  if (ptc != nullptr && IsLiveBase(tfe.EngineBase(*ptc)) && r.Pos() + 4 <= posEnd) {
    en.bHasHealth = true;
    en.fHealth = r.F32();
  }
}

}  // namespace

float MipFactorToDistance(float fFactor)
{
  if (fFactor == 100.0f) return 1E6f;
  return 320.0f * powf(2.0f, fFactor) / 1024.0f;   // as CBrushMip::Read_new_t converts it
}

void ReadTfeWorld(const Bytes &ab, const TfeClasses &tfe, World &wo, Report &rep)
{
  Reader r(ab);
  if (r.IsID("WRLD")) r.Fail("this file has no engine build header; it looks like an alpha world already");
  r.ExpectID("BUIV");
  int32_t iBuild = r.I32();
  if (iBuild < 10000) rep.Add("Notes", "engine build " + std::to_string(iBuild) + " (First Encounter worlds are build 10000)");
  r.ExpectID("WRLD");
  if (r.OptionalID("WLIF")) {
    r.OptionalID("DTRS");
    wo.strName = r.String();
    wo.ulSpawnFlags = r.U32();
    wo.strDescription = r.String();
  }

  std::vector<std::string> astrBrushDict;
  size_t posEnd = r.BeginDictionary(astrBrushDict);
  ReadBrushes(r, wo);
  r.EndDictionary(posEnd);

  // world state
  std::vector<std::string> astrStateDict;
  posEnd = r.BeginDictionary(astrStateDict);
  r.ExpectID("WSTA");
  int32_t iStateVersion = r.I32();
  if (iStateVersion != 9) r.Fail("world state version " + std::to_string(iStateVersion) + " is not supported (expected 9)");
  if (r.OptionalID("WLIF")) {
    r.OptionalID("DTRS");
    wo.strName = r.String();
    wo.ulSpawnFlags = r.U32();
    wo.strDescription = r.String();
  } else {
    wo.strDescription = r.String();
  }
  wo.colBackground = r.U32();
  if (r.OptionalID("NFID")) r.U32();        // next entity ID
  r.ExpectID("BGVW");
  wo.iBackgroundViewer = r.I32();
  r.ExpectID("BRDP");
  for (std::string &str : wo.astrBackdrop) str = r.String();
  r.Raw(wo.afBackdrop, sizeof(wo.afBackdrop));
  r.ExpectID("BDRO");
  wo.strBackdropObject = r.String();
  r.ExpectID("VWPS");
  ReadPlacement(r, wo.plViewer);
  wo.fViewerDistance = r.F32();
  if (r.OptionalID("TBPS")) {
    wo.bHasThumbnail = true;
    ReadPlacement(r, wo.plThumbnail);
    wo.fThumbnailDistance = r.F32();
  }

  bool bByID = false;
  if (!r.OptionalID("ENTs")) {
    r.ExpectID("ENs2");
    bByID = true;
  }
  int32_t ctEntities = r.Count(28);
  wo.aentities.resize(ctEntities);
  for (int32_t i = 0; i < ctEntities; i++) {
    Entity &en = wo.aentities[i];
    en.ulID = bByID ? r.U32() : (uint32_t)i;
    en.strClass = r.FileName();
    ReadPlacement(r, en.pl);
  }
  if (!bByID) rep.Add("Notes", "entities are stored by index (older TFE format)");

  for (Entity &en : wo.aentities) {
    if (!r.IsID("ENT4")) r.Fail("entity data in an older format (" + Reader::Printable(r.PeekID()) + ") is not supported");
    r.ExpectID("ENT4");
    r.U32();                                // entity ID again
    size_t slSize = (size_t)r.Count(1);
    size_t posEntityEnd = r.Pos() + slSize;
    try {
      ReadEntityBody(r, tfe, en, posEntityEnd);
    } catch (const FormatError &e) {
      en.strClass = "!" + en.strClass;      // marks an entity that couldn't be read
      rep.Add("Dropped entities", FileBase(en.strClass.substr(1)) + ": cannot read it (" + e.what() + ")");
    }
    r.SetPos(posEntityEnd);
  }
  // the rest of the state (entity order, entity-sector links) is recomputed by the engine
  r.EndDictionary(posEnd);
  r.ExpectID("WEND");
}
