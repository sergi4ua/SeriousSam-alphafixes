#include "classes.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

std::string ToLower(std::string str)
{
  for (char &ch : str) ch = (char)tolower((unsigned char)ch);
  return str;
}

std::string FileBase(const std::string &strPath)
{
  size_t iSlash = strPath.find_last_of("\\/");
  std::string str = iSlash == std::string::npos ? strPath : strPath.substr(iSlash + 1);
  size_t iDot = str.rfind('.');
  return iDot == std::string::npos ? str : str.substr(0, iDot);
}

// --- TFE -----------------------------------------------------------------------

static const char *_astrTfeLines[] = {
#include "tfe_classes.inc"
};

static void SplitBars(const std::string &str, std::vector<std::string> &astr)
{
  astr.clear();
  size_t i = 0;
  for (;;) {
    size_t j = str.find('|', i);
    astr.push_back(str.substr(i, j == std::string::npos ? std::string::npos : j - i));
    if (j == std::string::npos) break;
    i = j + 1;
  }
}

TfeClasses::TfeClasses()
{
  std::vector<TfeEnumValue> *paenum = nullptr;
  std::vector<std::string> astr;
  for (const char *strLine : _astrTfeLines) {
    std::string str = strLine;
    if (str.size() < 2) continue;
    std::string strRest = str.substr(2);
    switch (str[0]) {
    case 'C': {
      std::istringstream ss(strRest);
      TfeClass tc;
      ss >> tc.iID >> tc.strCpp >> tc.strBase;
      std::getline(ss >> std::ws, tc.strName);
      m_mapCpp[ToLower(tc.strCpp)] = m_aclasses.size();
      if (tc.iID >= 0) m_mapID[tc.iID] = m_aclasses.size();
      m_aclasses.push_back(tc);
      break; }
    case 'P': {
      size_t iSpace = strRest.find(' ');
      TfeProperty tp;
      tp.iIndex = atoi(strRest.c_str());
      SplitBars(strRest.substr(iSpace + 1), astr);
      tp.strType = astr[0];
      tp.strIdent = astr.size() > 1 ? astr[1] : "";
      tp.strName = astr.size() > 2 ? astr[2] : "";
      m_aclasses.back().aprops.push_back(tp);
      break; }
    case 'E':
      paenum = &m_mapEnums[strRest];
      break;
    case 'V': {
      size_t iSpace = strRest.find(' ');
      TfeEnumValue ev;
      ev.iValue = atoi(strRest.c_str());
      SplitBars(strRest.substr(iSpace + 1), astr);
      ev.strIdent = astr[0];
      ev.strName = astr.size() > 1 ? astr[1] : "";
      if (paenum != nullptr) paenum->push_back(ev);
      break; }
    }
  }
}

const TfeClass *TfeClasses::ForCpp(const std::string &strCpp) const
{
  auto it = m_mapCpp.find(ToLower(strCpp));
  return it == m_mapCpp.end() ? nullptr : &m_aclasses[it->second];
}

const TfeClass *TfeClasses::ForFile(const std::string &strEcl) const
{
  return ForCpp("C" + FileBase(strEcl));
}

const TfeProperty *TfeClasses::Property(const TfeClass *ptcEntity, uint32_t ulID) const
{
  const TfeClass *ptcOwner = nullptr;
  for (const TfeClass *ptc = ptcEntity; ptc != nullptr; ptc = ForCpp(ptc->strBase)) {
    if (ptc->iID == (int)(ulID >> 8)) { ptcOwner = ptc; break; }
  }
  if (ptcOwner == nullptr) {
    auto it = m_mapID.find((int)(ulID >> 8));
    if (it == m_mapID.end()) return nullptr;
    ptcOwner = &m_aclasses[it->second];
  }
  for (const TfeProperty &tp : ptcOwner->aprops) {
    if (tp.iIndex == (int)(ulID & 0xFF)) return &tp;
  }
  return nullptr;
}

const std::vector<TfeEnumValue> *TfeClasses::Enum(const std::string &strName) const
{
  auto it = m_mapEnums.find(strName);
  return it == m_mapEnums.end() ? nullptr : &it->second;
}

static bool IsEngineClass(const std::string &str)
{
  return str == "CEntity" || str == "CLiveEntity" || str == "CRationalEntity" || str == "CMovableEntity"
      || str == "CMovableModelEntity" || str == "CMovableBrushEntity" || str == "CPlayerEntity";
}

std::string TfeClasses::EngineBase(const TfeClass &tc) const
{
  const TfeClass *ptc = &tc;
  for (int i = 0; i < 32; i++) {
    if (IsEngineClass(ptc->strBase)) return ptc->strBase;
    ptc = ForCpp(ptc->strBase);
    if (ptc == nullptr) break;
  }
  return "CEntity";
}

// --- alpha ---------------------------------------------------------------------

// The alpha's CDLLEntityClass and CEntityProperty (Engine.dll, 13 Jan 2000).
// CEntityProperty is 28 bytes: it has no ep_ulFlags, unlike later versions.
struct DLLEntityProperty {
  int32_t ep_eptType;
  struct DLLEnumType *ep_pepetEnumType;
  uint32_t ep_ulID;
  int32_t ep_slOffset;
  const char *ep_strName;
  uint32_t ep_colColor;
  char ep_chShortcut;
};
static_assert(sizeof(DLLEntityProperty) == 28, "CEntityProperty is 28 bytes in the alpha");

struct DLLEnumValue { int32_t epev_iValue; const char *epev_strName; };
struct DLLEnumType { DLLEnumValue *epet_aepevValues; int32_t epet_ctValues; };

struct DLLEntityClass {
  DLLEntityProperty *dec_aepProperties;
  int32_t dec_ctProperties;
  void *dec_aeheHandlers;
  int32_t dec_ctHandlers;
  void *dec_aecComponents;
  int32_t dec_ctComponents;
  const char *dec_strName;
  const char *dec_strIconFileName;
  int32_t dec_iID;
  DLLEntityClass *dec_pdecBase;
};

bool AlphaClasses::Load(const std::string &strGameDir, std::string &strError)
{
  m_strGameDir = strGameDir;
  std::string strBin = strGameDir + "\\Bin";
  SetDllDirectoryA(strBin.c_str());
  m_hEngine = LoadLibraryExA((strBin + "\\Engine.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (m_hEngine == nullptr) {
    strError = "cannot load " + strBin + "\\Engine.dll (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  m_hEntities = LoadLibraryExA((strBin + "\\Entities.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (m_hEntities == nullptr) {
    strError = "cannot load " + strBin + "\\Entities.dll (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  return true;
}

static bool ReadTextFile(const std::string &strPath, std::string &str)
{
  std::ifstream f(strPath, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  str = ss.str();
  return true;
}

const AlphaClass *AlphaClasses::ForFile(const std::string &strEcl)
{
  std::string strKey = ToLower(strEcl);
  auto it = m_mapClasses.find(strKey);
  if (it != m_mapClasses.end()) return &it->second;
  if (m_mapMissing.count(strKey)) return nullptr;

  // Classes\Name.ecl: "Package: TFNM Bin\Entities.dll" and "Class: CName"
  std::string strText;
  std::string strCpp;
  std::string strPackage;
  if (ReadTextFile(m_strGameDir + "\\" + strEcl, strText)) {
    size_t i = strText.find("Class:");
    if (i != std::string::npos) {
      std::istringstream ss(strText.substr(i + 6));
      ss >> strCpp;
    }
    i = strText.find("Package:");
    if (i != std::string::npos) {
      std::istringstream ss(strText.substr(i + 8));
      std::string strTag;
      ss >> strTag >> strPackage;   // "TFNM Bin\Name.dll"
    }
  }
  // Classes from another DLL than Entities.dll (such as AlphaFixesEntities.dll)
  // are looked up in that DLL; it imports Entities.dll for its base classes.
  void *hPackage = m_hEntities;
  std::string strPackageKey = ToLower(strPackage);
  if (!strPackageKey.empty() && strPackageKey != "bin\\entities.dll") {
    auto itPackage = m_mapPackages.find(strPackageKey);
    if (itPackage == m_mapPackages.end()) {
      void *h = LoadLibraryExA((m_strGameDir + "\\" + strPackage).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      itPackage = m_mapPackages.emplace(strPackageKey, h).first;
    }
    hPackage = itPackage->second;
  }
  DLLEntityClass *pdec = nullptr;
  if (!strCpp.empty() && hPackage != nullptr) {
    pdec = (DLLEntityClass *)GetProcAddress((HMODULE)hPackage, (strCpp + "_DLLClass").c_str());
  }
  if (pdec == nullptr) {
    m_mapMissing[strKey] = true;
    return nullptr;
  }

  static const char *astrEngine[] = {
    "CEntity", "CLiveEntity", "CRationalEntity", "CMovableEntity",
    "CMovableModelEntity", "CMovableBrushEntity", "CPlayerEntity",
  };
  AlphaClass ac;
  ac.strFile = strEcl;
  ac.strCpp = strCpp;
  ac.strName = pdec->dec_strName != nullptr ? pdec->dec_strName : "";
  ac.strEngineBase = "CEntity";
  // walk up to the first engine class (engine classes have no saved properties
  // that a level would set)
  for (DLLEntityClass *p = pdec; p != nullptr; p = p->dec_pdecBase) {
    const char *strEngineClass = nullptr;
    for (const char *strEngine : astrEngine) {
      if ((void *)p == (void *)GetProcAddress((HMODULE)m_hEngine, (std::string(strEngine) + "_DLLClass").c_str())) {
        strEngineClass = strEngine;
        break;
      }
    }
    if (strEngineClass != nullptr) {
      ac.strEngineBase = strEngineClass;
      break;
    }
    for (int32_t i = 0; i < p->dec_ctProperties; i++) {
      const DLLEntityProperty &ep = p->dec_aepProperties[i];
      AlphaProperty ap;
      ap.ept = (PropertyType)ep.ep_eptType;
      ap.ulID = ep.ep_ulID;
      ap.strName = ep.ep_strName != nullptr ? ep.ep_strName : "";
      if ((ap.ept == EPT_ENUM || ap.ept == EPT_FLAGS) && ep.ep_pepetEnumType != nullptr) {
        ap.bEnum = true;
        for (int32_t j = 0; j < ep.ep_pepetEnumType->epet_ctValues; j++) {
          const DLLEnumValue &ev = ep.ep_pepetEnumType->epet_aepevValues[j];
          ap.aenum.push_back({ev.epev_iValue, ev.epev_strName != nullptr ? ev.epev_strName : ""});
        }
      }
      ac.aprops.push_back(ap);
    }
  }
  return &(m_mapClasses[strKey] = ac);
}
