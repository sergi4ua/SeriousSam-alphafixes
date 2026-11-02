// Entity class tables of both games.
//
// TFE: generated from the First Encounter entity sources (tfe_classes.inc):
// class IDs, bases and the names of the properties a TFE world stores by ID.
//
// Alpha: read at run time from the alpha's own Bin\Entities.dll.  Its classes
// are CDLLEntityClass structures exported as C<Name>_DLLClass; their property
// tables and base class pointers are filled in by the DLL's static
// initializers, so the DLL is loaded (with Engine.dll, which it imports) and
// the tables are read from memory.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "world.h"

// --- TFE -----------------------------------------------------------------------

struct TfeProperty {
  int iIndex;
  std::string strType;                  // as written in the .es: FLOAT, enum LightType, ...
  std::string strIdent;                 // m_fStretchAll
  std::string strName;                  // editor name, empty for internal properties
};

struct TfeClass {
  int iID;
  std::string strCpp, strBase, strName;
  std::vector<TfeProperty> aprops;
};

struct TfeEnumValue { int iValue; std::string strIdent, strName; };

class TfeClasses {
public:
  TfeClasses();
  const TfeClass *ForFile(const std::string &strEcl) const;     // Classes\Name.ecl
  const TfeClass *ForCpp(const std::string &strCpp) const;
  // ulID: (class ID << 8) | index.  Class IDs aren't unique (AnimationChanger
  // and MirrorMarker are both 218), so the entity's class chain is searched first.
  const TfeProperty *Property(const TfeClass *ptcEntity, uint32_t ulID) const;
  const std::vector<TfeEnumValue> *Enum(const std::string &strName) const;
  // the engine class at the root of the class (CEntity, CRationalEntity, ...)
  std::string EngineBase(const TfeClass &tc) const;

private:
  std::vector<TfeClass> m_aclasses;
  std::map<std::string, size_t> m_mapCpp;                       // lower-case C++ name -> class
  std::map<int, size_t> m_mapID;
  std::map<std::string, std::vector<TfeEnumValue>> m_mapEnums;
};

// --- alpha ---------------------------------------------------------------------

struct AlphaEnumValue { int iValue; std::string strName; };

struct AlphaProperty {
  PropertyType ept;
  uint32_t ulID;
  std::string strName;                  // editor name
  bool bEnum = false;
  std::vector<AlphaEnumValue> aenum;
};

struct AlphaClass {
  std::string strFile;                  // Classes\Name.ecl
  std::string strCpp, strName;
  std::string strEngineBase;            // CEntity, CRationalEntity, CMovableModelEntity, ...
  std::vector<AlphaProperty> aprops;    // this class first, then its bases
};

class AlphaClasses {
public:
  // strGameDir: the alpha's folder (with Bin\ and Classes\)
  bool Load(const std::string &strGameDir, std::string &strError);
  // nullptr if the class doesn't exist in the alpha
  const AlphaClass *ForFile(const std::string &strEcl);
  const std::string &GameDir() const { return m_strGameDir; }

private:
  std::string m_strGameDir;
  void *m_hEngine = nullptr;
  void *m_hEntities = nullptr;
  std::map<std::string, void *> m_mapPackages;                  // lower-case DLL path -> other class DLLs
  std::map<std::string, AlphaClass> m_mapClasses;               // lower-case .ecl path -> class
  std::map<std::string, bool> m_mapMissing;
};

std::string ToLower(std::string str);
// file name part without the extension: Classes\ModelHolder2.ecl -> ModelHolder2
std::string FileBase(const std::string &strPath);
