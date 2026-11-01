// Mapping a TFE world onto what the alpha has: entity classes and properties,
// textures and the world tables (blending, scrolling, surfaces, contents)
// that polygons and sectors refer to by index.
#include "convert.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <set>

namespace {

// --- world tables ---------------------------------------------------------------
// TFE (Entities/WorldBase.es) and alpha (WorldBase in its Entities.dll) define
// these tables in the same order up to where the alpha's end; TFE's later
// entries are mapped by name or kind.

const char *_astrTfeBlendings[] = {
  "Opaque", "Shade", "Blend", "Add", "Shade pulsating", "Blend pulsating full", "Add pulsating",
  "Blend pulsating half", "Stormy sky blend", "Stormy sky shade", "Pyramid plate appearing",
  "Activated plate 1", "Activated plate 2", "Activated plate 3", "Activated plate 4",
  "Activate pyramid morph room",
};
// what TFE's extra blendings become: the alpha blending of the same kind (alpha/shade/add)
const uint8_t _aubTfeBlendingToAlpha[] = { 0, 1, 2, 3, 4, 5, 6, 7, 2, 1, 2, 3, 3, 3, 3, 2 };
const int ctAlphaBlendings = 8;

const char *_astrTfeScrolls[] = {
  "None", "R Extremly Slow", "R Very Slow", "R Slow", "R Medium", "R Fast", "R Very Fast", "R Extremly Fast",
  "Dummy 1", "Dummy 2", "Dummy 3", "Water movement extremly slow", "Water movement very slow",
  "Water movement slow", "Water movement normal", "Water movement fast", "Stormy sky appearing",
  "Rotation Left 1", "Rotation Left 2", "Rotation Left 3", "Rotation Left 4", "Rotation Left 5",
  "Rotation Left 6", "Rotation Left 7", "Rotation Left 8", "Rotation Left 9", "Rotation Left 10",
  "Rotation Right 1", "Rotation Right 2", "Rotation Right 3", "Rotation Right 4", "Rotation Right 5",
  "Rotation Right 6", "Rotation Right 7", "Rotation Right 8", "Rotation Right 9", "Rotation Right 10",
  "D Extremly Slow", "D Very Slow", "D Slow", "D Medium", "D Fast", "D Very Fast", "D Extremly Fast",
  "D Super Fast", "D Abnormaly Fast",
};
const int ctSameScrolls = 8;            // alpha: None, R ... (8), then its own Rotation entries

const char *_astrTfeSurfaces[] = {
  "Standard", "Ice", "Standard - no step", "Standard - high stairs", "Ice climbable slope",
  "Ice sliding slope", "Ice less sliding", "Roller coaster", "Lava", "Sand", "Clibamble Slope",
  "Standard - no impact", "Water", "Red sand",
};
const int ctSameSurfaces = 9;

const char *_astrTfeContents[] = { "Air", "Water", "Lava", "Cold Water", "Spikes", "Desert heat" };
const int ctSameContents = 4;

const int ctSameIlluminations = 10;     // None, Vitraj 1-3, Lava 1-3, Misc 1-3 in both

template<size_t N> std::string TableName(const char *(&astr)[N], int i)
{
  return (i >= 0 && (size_t)i < N) ? std::string("'") + astr[i] + "'" : "#" + std::to_string(i);
}

// bits of the TSE/TFE polygon flags that are internal, editing state or unknown to the alpha
const uint32_t BPOF_STAIRS = 1u << 7;
const uint32_t POLYGON_FLAGS_CLEAR =
  (1u << 6) | BPOF_STAIRS | (1u << 8) | (1u << 9) | (1u << 19) | (1u << 25) | (1u << 29) | (1u << 30) | (1u << 31);
// sector flags: selection and temporary bits
const uint32_t SECTOR_FLAGS_CLEAR = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 6) | (1u << 7);
// entity flags kept for the alpha: zoning, alive, see-through, cluster shadows, background, anchored
const uint32_t ENTITY_FLAGS_KEEP = (1u << 1) | (1u << 3) | (1u << 6) | (1u << 8) | (1u << 9) | (1u << 10);

std::string Norm(const std::string &str)
{
  // lower case letters and digits only, numbers without leading zeros
  // ("Target 01" = "target1")
  std::string out;
  for (size_t i = 0; i < str.size(); i++) {
    unsigned char ch = (unsigned char)str[i];
    if (!isalnum(ch)) continue;
    bool bNumberStart = i == 0 || !isdigit((unsigned char)str[i - 1]);
    if (ch == '0' && bNumberStart && i + 1 < str.size() && isdigit((unsigned char)str[i + 1])) continue;
    out += (char)tolower(ch);
  }
  return out;
}

const char *TypeName(PropertyType ept)
{
  switch (ept) {
  case EPT_ENUM: return "enum"; case EPT_BOOL: return "BOOL"; case EPT_FLOAT: return "FLOAT";
  case EPT_COLOR: return "COLOR"; case EPT_STRING: return "CTString"; case EPT_RANGE: return "RANGE";
  case EPT_ENTITYPTR: return "CEntityPointer"; case EPT_FILENAME: return "CTFileName";
  case EPT_INDEX: return "INDEX"; case EPT_ANIMATION: return "ANIMATION";
  case EPT_ILLUMINATIONTYPE: return "ILLUMINATIONTYPE"; case EPT_FLOATAABBOX3D: return "FLOATaabbox3D";
  case EPT_ANGLE: return "ANGLE"; case EPT_FLOAT3D: return "FLOAT3D"; case EPT_ANGLE3D: return "ANGLE3D";
  case EPT_FLOATplane3D: return "FLOATplane3D"; case EPT_MODELOBJECT: return "CModelObject";
  case EPT_PLACEMENT3D: return "CPlacement3D"; case EPT_ANIMOBJECT: return "CAnimObject";
  case EPT_FILENAMENODEP: return "CTFileNameNoDep"; case EPT_SOUNDOBJECT: return "CSoundObject";
  case EPT_STRINGTRANS: return "CTStringTrans"; case EPT_FLOATQUAT3D: return "FLOATquat3D";
  case EPT_FLOATMATRIX3D: return "FLOATmatrix3D"; case EPT_FLAGS: return "flags";
  default: return "?";
  }
}

// the value kinds a property can be converted between
int ValueKind(PropertyType ept)
{
  switch (ept) {
  case EPT_STRING: case EPT_STRINGTRANS: case EPT_FILENAMENODEP: return 1;
  case EPT_FLOAT: case EPT_RANGE: return 2;
  default: return 100 + ept;
  }
}

// Some properties were renamed between the alpha and TFE: TFE class, TFE editor
// name -> alpha editor name.  An empty class matches any class.
struct Alias { const char *strClass, *strTfe, *strAlpha; };
const Alias _aaliases[] = {
  { "TouchField", "Enter Target", "Target" },
  { "TouchField", "Enter Event", "Event Type" },
  { "MusicHolder", "Music Light", "Peace Music" },
  { "MusicHolder", "Music Heavy", "Fight Music" },
  { "AnimationChanger", "Diffuse Light Animation", "Light Animation" },
  { "AnimationChanger", "Diffuse Light Looping", "Light Looping" },
};

class Converter {
public:
  Converter(World &wo, const TfeClasses &tfe, AlphaClasses &alpha, const Options &opt, OutWorld &out, Report &rep)
    : m_wo(wo), m_tfe(tfe), m_alpha(alpha), m_opt(opt), m_out(out), m_rep(rep) {}

  void Run()
  {
    m_out.pwoSource = &m_wo;
    if (!m_wo.strName.empty()) {
      m_rep.Add("World", "name '" + m_wo.strName + "' dropped (alpha worlds have no name, only the description)");
    }
    if (m_wo.ulSpawnFlags != 0) {
      m_rep.Add("World", "game mode flags dropped (the alpha has no per-world spawn flags)");
    }
    SelectEntities();
    for (size_t i = 0; i < m_wo.aentities.size(); i++) {
      if (m_aiNewIndex[i] >= 0) ConvertEntity(m_wo.aentities[i], m_out.aentities[m_aiNewIndex[i]]);
    }
    auto it = m_mapIDToIndex.find((uint32_t)m_wo.iBackgroundViewer);
    m_out.iBackgroundViewer = (m_wo.iBackgroundViewer != -1 && it != m_mapIDToIndex.end()) ? it->second : -1;
    if (m_wo.iBackgroundViewer != -1 && m_out.iBackgroundViewer == -1) {
      m_rep.Add("World", "background viewer dropped (its entity wasn't converted)");
    }
    for (const auto &item : m_mapInternal) {
      std::string str;
      for (const std::string &strIdent : item.second) str += (str.empty() ? "" : ", ") + strIdent;
      m_rep.Add("Run-time properties not converted (no editor name; the alpha's initialization sets its own)", item.first + ": " + str);
    }
    ConvertBrushes();
    ConvertShadowLayers();
    RemoveUnusedBrushes();
  }

private:
  bool AlphaFileExists(const std::string &strRel)
  {
    std::string strKey = ToLower(strRel);
    auto it = m_mapFiles.find(strKey);
    if (it != m_mapFiles.end()) return it->second;
    DWORD dw = GetFileAttributesA((m_opt.strGameDir + "\\" + strRel).c_str());
    bool b = dw != INVALID_FILE_ATTRIBUTES && !(dw & FILE_ATTRIBUTE_DIRECTORY);
    // a model or texture in a format the alpha doesn't read (copied from a later version)
    std::string strExt = strKey.size() > 4 ? strKey.substr(strKey.size() - 4) : "";
    if (b && m_opt.pEngine != nullptr && (strExt == ".tex" || strExt == ".mdl") && !m_opt.pEngine->CanLoad(strRel)) {
      m_rep.Add("Files the alpha can't load (in a later format), treated as missing", strRel);
      b = false;
    }
    m_mapFiles[strKey] = b;
    return b;
  }

  // --- which entities are converted --------------------------------------------

  void SelectEntities()
  {
    const AlphaClass *pacWorldBase = m_alpha.ForFile("Classes\\WorldBase.ecl");
    m_aiNewIndex.assign(m_wo.aentities.size(), -1);
    for (size_t i = 0; i < m_wo.aentities.size(); i++) {
      Entity &en = m_wo.aentities[i];
      if (!en.strClass.empty() && en.strClass[0] == '!') continue;   // unreadable, reported already
      std::string strName = FileBase(en.strClass);
      bool bBrush = en.ulRenderType == RT_BRUSH || en.ulRenderType == RT_FIELDBRUSH;
      if (en.ulFlags & (1u << 2)) {         // ENF_DELETED
        m_rep.Add("Dropped entities", strName + ": deleted entity (kept by TFE only for references)");
        continue;
      }
      if (bBrush && (en.iBrush < 0 || (size_t)en.iBrush >= m_wo.abrushes.size())) {
        m_rep.Add("Dropped entities", strName + ": bad brush index");
        continue;
      }
      const AlphaClass *pac = m_alpha.ForFile(en.strClass);
      if (pac != nullptr && (pac->strEngineBase == "CPlayerEntity" || pac->strCpp == "CGhostBusterRay")) {
        m_rep.Add("Dropped entities", strName + ": runtime-only class, not placed in levels");
        continue;
      }
      if (pac == nullptr && bBrush) {
        // keep the geometry: the brush can't exist without an entity
        if (pacWorldBase == nullptr) {
          m_rep.Add("Dropped entities", strName + ": class not in the alpha");
          continue;
        }
        pac = pacWorldBase;
        m_rep.Add("Changed entities", strName + " (brush): class not in the alpha, became a WorldBase brush");
      }
      if (pac == nullptr) {
        m_rep.Add("Dropped entities", strName + ": class not in the alpha");
        continue;
      }
      if (!bBrush && !m_opt.bAllSpawnFlags && (en.ulSpawnFlags & m_opt.ulSpawnMask) != m_opt.ulSpawnMask) {
        m_rep.Add("Dropped entities", strName + ": not active in single player at normal difficulty (spawn flags)");
        continue;
      }
      m_aiNewIndex[i] = (int32_t)m_out.aentities.size();
      m_mapIDToIndex[en.ulID] = (int32_t)m_out.aentities.size();
      OutEntity oe;
      oe.pac = pac;
      m_out.aentities.push_back(oe);
      m_rep.Add("Converted entities", pac->strName.empty() ? strName : FileBase(pac->strFile));
      if (bBrush) m_abBrushUsed.insert(en.iBrush);
    }
  }

  // --- one entity ---------------------------------------------------------------

  void ConvertEntity(const Entity &en, OutEntity &oe)
  {
    oe.pl = en.pl;
    oe.ulPhysicsFlags = en.ulPhysicsFlags;
    oe.ulCollisionFlags = en.ulCollisionFlags;
    oe.ulFlags = en.ulFlags & ENTITY_FLAGS_KEEP;
    if (en.ulRenderType == RT_BRUSH || en.ulRenderType == RT_FIELDBRUSH) {
      oe.ulRenderType = en.ulRenderType;
      oe.iBrush = en.iBrush;
    } else {
      // Models are set up by the entity's own initialization in the alpha
      // (the engine pass), from its properties.  Until then it is a void.
      oe.ulRenderType = RT_VOID;
    }
    if (en.bHasParent) {
      auto it = m_mapIDToIndex.find(en.ulParent);
      if (it != m_mapIDToIndex.end()) {
        oe.bHasParent = true;
        oe.iParent = it->second;
        oe.plRelative = en.plRelative;
      } else {
        m_rep.Add("Changed entities", FileBase(en.strClass) + ": parent wasn't converted, unparented (stays in place)");
      }
    }
    if (en.bHasHealth) oe.fHealth = en.fHealth;
    if (en.bHasLight && oe.pac->strCpp == "CLight") {
      oe.bHasLight = true;
      oe.alayers = en.alayers;
    }

    std::string strClass = FileBase(en.strClass);
    const TfeClass *ptc = m_tfe.ForFile(en.strClass);
    if (ptc == nullptr) m_rep.Add("Notes", strClass + ": class unknown to the converter's TFE tables, its properties can't be matched");
    m_bModelReplaced = false;
    // model file first, so that a replaced model also replaces its texture
    std::vector<const Property *> apsorted;
    for (const Property &p : en.aprops) {
      const TfeProperty *ptp = m_tfe.Property(ptc, p.ulID);
      bool bModel = p.ept == EPT_FILENAME && ptp != nullptr && Norm(ptp->strName) == "model";
      if (bModel) apsorted.insert(apsorted.begin(), &p); else apsorted.push_back(&p);
    }
    for (const Property *pp : apsorted) {
      OutProperty op;
      if (ConvertProperty(strClass, ptc, *pp, *oe.pac, op)) oe.aprops.push_back(op);
    }
  }

  const AlphaProperty *FindAlphaProperty(const std::string &strClass, const std::string &strTfeName, const AlphaClass &ac)
  {
    std::string strWanted = Norm(strTfeName);
    for (const Alias &al : _aaliases) {
      if ((al.strClass[0] == 0 || strClass == al.strClass) && Norm(al.strTfe) == strWanted) {
        strWanted = Norm(al.strAlpha);
        break;
      }
    }
    for (const AlphaProperty &ap : ac.aprops) {
      if (!ap.strName.empty() && Norm(ap.strName) == strWanted) return &ap;
    }
    return nullptr;
  }

  bool ConvertProperty(const std::string &strClass, const TfeClass *ptc, const Property &p, const AlphaClass &ac, OutProperty &op)
  {
    const TfeProperty *ptp = m_tfe.Property(ptc, p.ulID);
    if (ptp == nullptr) {
      // not in the TFE sources (an engine-internal or later-version property)
      m_mapInternal[strClass].insert("#" + std::to_string(p.ulID >> 8) + "/" + std::to_string(p.ulID & 0xFF));
      return false;
    }
    std::string strWhat = strClass + "." + ptp->strIdent + (ptp->strName.empty() ? "" : " \"" + ptp->strName + "\"");
    if (ptp->strName.empty() || p.ept == EPT_MODELOBJECT || p.ept == EPT_ANIMOBJECT || p.ept == EPT_SOUNDOBJECT) {
      // run-time state of the TFE class; the alpha's initialization sets its own
      m_mapInternal[strClass].insert(ptp->strIdent);
      return false;
    }
    const AlphaProperty *pap = FindAlphaProperty(strClass, ptp->strName, ac);
    if (pap == nullptr) {
      m_rep.Add("Dropped properties (no such property in the alpha class)", strWhat);
      return false;
    }
    if (ValueKind(p.ept) != ValueKind(pap->ept)) {
      m_rep.Add("Dropped properties (different type in the alpha)", strWhat + ": " + TypeName(p.ept) + " -> " + TypeName(pap->ept));
      return false;
    }
    op.ept = pap->ept;
    op.ulID = pap->ulID;
    op.iValue = p.iValue;
    op.fValue = p.fValue;
    op.strValue = p.strValue;
    op.abValue = p.abValue;

    switch (pap->ept) {
    case EPT_ENTITYPTR:
      if (p.iValue != -1) {
        auto it = m_mapIDToIndex.find((uint32_t)p.iValue);
        if (it == m_mapIDToIndex.end()) {
          op.iValue = -1;
          m_rep.Add("Changed properties", strWhat + ": target wasn't converted, cleared");
        } else {
          op.iValue = it->second;
        }
      }
      break;
    case EPT_ENUM:
      return ConvertEnum(strWhat, *ptp, p, *pap, op);
    case EPT_ILLUMINATIONTYPE:
      if (p.iValue < 0 || p.iValue >= ctSameIlluminations) {
        op.iValue = 0;
        m_rep.Add("Changed properties", strWhat + ": illumination type " + std::to_string(p.iValue) + " -> None");
      }
      break;
    case EPT_FILENAME:
      ConvertFileName(strWhat, op);
      break;
    default:
      break;
    }
    return true;
  }

  bool ConvertEnum(const std::string &strWhat, const TfeProperty &tp, const Property &p, const AlphaProperty &ap, OutProperty &op)
  {
    std::string strEnum = tp.strType.size() > 5 ? tp.strType.substr(5) : "";   // "enum Name"
    const std::vector<TfeEnumValue> *paev = m_tfe.Enum(strEnum);
    const TfeEnumValue *pev = nullptr;
    if (paev != nullptr) {
      for (const TfeEnumValue &ev : *paev) {
        if (ev.iValue == p.iValue) { pev = &ev; break; }
      }
    }
    if (pev == nullptr) {
      m_rep.Add("Dropped properties (enum value not found)", strWhat + " = " + std::to_string(p.iValue));
      return false;
    }
    for (const AlphaEnumValue &av : ap.aenum) {
      if (Norm(av.strName) == Norm(pev->strName)) {
        op.iValue = av.iValue;
        if (av.iValue != p.iValue) m_rep.Add("Changed properties", strWhat + " = '" + pev->strName + "': value " + std::to_string(p.iValue) + " -> " + std::to_string(av.iValue));
        return true;
      }
    }
    m_rep.Add("Dropped properties (no such choice in the alpha)", strWhat + " = '" + pev->strName + "'");
    return false;
  }

  void ConvertFileName(const std::string &strWhat, OutProperty &op)
  {
    std::string &str = op.strValue;
    if (str.empty()) return;
    std::string strExt = ToLower(str.substr(str.find_last_of('.') == std::string::npos ? str.size() : str.find_last_of('.')));
    if (m_bModelReplaced && strExt == ".tex") {
      // the texture belonged to the replaced model
      str = "Models\\Editor\\Vector.tex";
      return;
    }
    if (AlphaFileExists(str)) return;
    if (strExt == ".mdl" && AlphaFileExists("Models\\Editor\\Axis.mdl")) {
      m_rep.Add("Missing files (models replaced by the editor's axis model)", str);
      str = "Models\\Editor\\Axis.mdl";
      m_bModelReplaced = true;
      return;
    }
    m_rep.Add("Missing files (properties cleared)", str + "  [" + strWhat + "]");
    str = "";
  }

  // --- brushes ------------------------------------------------------------------

  void ConvertBrushes()
  {
    for (size_t ib = 0; ib < m_wo.abrushes.size(); ib++) {
      for (BrushMip &bm : m_wo.abrushes[ib].amips) {
        for (Sector &sc : bm.asectors) {
          sc.ulFlags &= ~SECTOR_FLAGS_CLEAR;
          int iContent = (int)(sc.ulFlags >> 24);
          if (iContent >= ctSameContents) {
            m_rep.Add("Changed sectors", "content type " + TableName(_astrTfeContents, iContent) + " -> 'Air'");
            sc.ulFlags &= 0x00FFFFFFu;
          }
          for (BrushPolygon &po : sc.apolygons) ConvertPolygon(po);
        }
      }
    }
  }

  uint8_t MapBlending(uint8_t ub, const char *strWhat)
  {
    if (ub < ctAlphaBlendings) return ub;
    uint8_t ubNew = ub < sizeof(_aubTfeBlendingToAlpha) ? _aubTfeBlendingToAlpha[ub] : 0;
    m_rep.Add("Changed polygons", std::string(strWhat) + " " + TableName(_astrTfeBlendings, ub) + " -> " + TableName(_astrTfeBlendings, ubNew));
    return ubNew;
  }

  void ConvertPolygon(BrushPolygon &po)
  {
    if (po.ulFlags & BPOF_STAIRS) m_rep.Add("Changed polygons", "'stairs' flag dropped (the alpha has no stairs polygons)");
    po.ulFlags &= ~POLYGON_FLAGS_CLEAR;
    for (int it = 0; it < 3; it++) {
      Texture &tex = po.atex[it];
      if (!tex.strFile.empty() && !AlphaFileExists(tex.strFile)) {
        m_rep.Add("Missing textures (polygon layers left empty)", tex.strFile);
        tex.strFile = "";
      }
      if (tex.ubScroll >= ctSameScrolls) {
        m_rep.Add("Changed polygons", "texture scrolling " + TableName(_astrTfeScrolls, tex.ubScroll) + " -> 'None'");
        tex.ubScroll = 0;
      }
      tex.ubBlend = MapBlending(tex.ubBlend, "texture blending");
      if (tex.ubFlags & ~0x0F) m_rep.Add("Changed polygons", "texture reflection flag dropped");
      tex.ubFlags &= 0x0F;   // clamp u/v, discardable, after shadow
    }
    uint8_t &ubSurface = po.aubProperties[0];
    if (ubSurface >= ctSameSurfaces) {
      m_rep.Add("Changed polygons", "surface " + TableName(_astrTfeSurfaces, ubSurface) + " -> 'Standard'");
      ubSurface = 0;
    }
    uint8_t &ubIllumination = po.aubProperties[1];
    if (ubIllumination >= ctSameIlluminations) {
      m_rep.Add("Changed polygons", "illumination type #" + std::to_string(ubIllumination) + " -> 'None'");
      ubIllumination = 0;
    }
    po.aubProperties[2] = MapBlending(po.aubProperties[2], "shadow blending");
    uint32_t ulShadowFlags;
    memcpy(&ulShadowFlags, po.aubShadowMap, 4);
    ulShadowFlags &= 0x3;    // dynamic shadow state; the rest is TFE runtime state
    memcpy(po.aubShadowMap, &ulShadowFlags, 4);
  }

  // Shadow layers belong to light sources; each light lists its layers by
  // polygon and position in the polygon's layer list.  The alpha has no
  // cleanup for layers without a light (a crash when they are drawn), so
  // only layers of converted lights are kept, and their positions renumbered.
  void ConvertShadowLayers()
  {
    std::set<std::vector<int32_t>> setKept;
    for (const OutEntity &oe : m_out.aentities) {
      for (const LightLayerRef &ll : oe.alayers) setKept.insert({ll.iBrush, ll.iMip, ll.iSector, ll.iPolygon, ll.iLayer});
    }
    std::map<std::vector<int32_t>, int32_t> mapNewIndex;
    int ctDropped = 0, ctKept = 0;
    for (int32_t ib = 0; ib < (int32_t)m_wo.abrushes.size(); ib++) {
      Brush &br = m_wo.abrushes[ib];
      for (int32_t im = 0; im < (int32_t)br.amips.size(); im++) {
        for (int32_t is = 0; is < (int32_t)br.amips[im].asectors.size(); is++) {
          Sector &sc = br.amips[im].asectors[is];
          for (int32_t ip = 0; ip < (int32_t)sc.apolygons.size(); ip++) {
            std::vector<ShadowLayer> &alayers = sc.apolygons[ip].alayers;
            std::vector<ShadowLayer> akept;
            for (int32_t il = 0; il < (int32_t)alayers.size(); il++) {
              std::vector<int32_t> key = {ib, im + 1, is, ip, il};   // lights count mips from 1
              if (setKept.count(key)) {
                mapNewIndex[key] = (int32_t)akept.size();
                akept.push_back(alayers[il]);
                ctKept++;
              } else {
                ctDropped++;
              }
            }
            alayers.swap(akept);
          }
        }
      }
    }
    for (OutEntity &oe : m_out.aentities) {
      std::vector<LightLayerRef> akept;
      for (LightLayerRef ll : oe.alayers) {
        auto it = mapNewIndex.find({ll.iBrush, ll.iMip, ll.iSector, ll.iPolygon, ll.iLayer});
        if (it == mapNewIndex.end()) continue;   // a polygon index out of range
        ll.iLayer = it->second;
        akept.push_back(ll);
      }
      oe.alayers.swap(akept);
    }
    m_rep.Add("Shadows", std::to_string(ctKept) + " shadow layers of converted lights kept");
    if (ctDropped > 0) {
      m_rep.Add("Shadows", std::to_string(ctDropped) + " shadow layers without a converted light dropped");
    }
  }

  // A brush whose entity wasn't converted would be left without an entity,
  // which the alpha doesn't expect: remove it and renumber the others.  Their
  // portal-sector links are then recomputed by the engine on loading.
  void RemoveUnusedBrushes()
  {
    if (m_abBrushUsed.size() == m_wo.abrushes.size()) return;
    std::vector<int32_t> aiNew(m_wo.abrushes.size(), -1);
    std::vector<Brush> akept;
    for (size_t ib = 0; ib < m_wo.abrushes.size(); ib++) {
      if (m_abBrushUsed.count((int32_t)ib)) {
        aiNew[ib] = (int32_t)akept.size();
        akept.push_back(std::move(m_wo.abrushes[ib]));
      }
    }
    m_rep.Add("Dropped brushes", std::to_string(m_wo.abrushes.size() - akept.size()) + " brushes whose entity wasn't converted");
    m_wo.abrushes.swap(akept);
    m_wo.bHasPortalLinks = false;
    for (OutEntity &oe : m_out.aentities) {
      if (oe.iBrush >= 0) oe.iBrush = aiNew[oe.iBrush];
      std::vector<LightLayerRef> alayers;
      for (LightLayerRef ll : oe.alayers) {
        ll.iBrush = aiNew[ll.iBrush];
        if (ll.iBrush >= 0) alayers.push_back(ll);
      }
      oe.alayers.swap(alayers);
    }
  }

  World &m_wo;
  const TfeClasses &m_tfe;
  AlphaClasses &m_alpha;
  const Options &m_opt;
  OutWorld &m_out;
  Report &m_rep;
  std::vector<int32_t> m_aiNewIndex;
  std::map<uint32_t, int32_t> m_mapIDToIndex;
  std::set<int32_t> m_abBrushUsed;
  std::map<std::string, bool> m_mapFiles;
  bool m_bModelReplaced = false;
  std::map<std::string, std::set<std::string>> m_mapInternal;   // class -> run-time properties not converted
};

}  // namespace

void ConvertWorld(World &wo, const TfeClasses &tfe, AlphaClasses &alpha, const Options &opt, OutWorld &out, Report &rep)
{
  Converter(wo, tfe, alpha, opt, out, rep).Run();
}
