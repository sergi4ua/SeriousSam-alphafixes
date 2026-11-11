// TFE -> alpha world conversion.
#pragma once

#include <cmath>
#include <string>

#include "classes.h"
#include "report.h"
#include "world.h"

class AlphaEngine;

struct Options {
  std::string strGameDir;               // the alpha's folder
  AlphaEngine *pEngine = nullptr;       // if running: models and textures must load in it
  uint32_t ulSpawnMask = (1u << 1) | (1u << 16);   // entities must be active at normal difficulty in single player
  bool bDropByDifficulty = false;       // --difficulty given: also drop entities not active at it
  bool bAllSpawnFlags = false;          // keep entities whatever their spawn flags
  bool bAlphaTrigger = false;           // TFE triggers become the alpha's Trigger, not Trigger2
};

// an entity as the alpha will read it
struct OutProperty {
  PropertyType ept;
  uint32_t ulID;
  int32_t iValue = 0;
  float fValue = 0;
  std::string strValue;
  Bytes abValue;
};

struct OutEntity {
  const AlphaClass *pac = nullptr;
  Placement pl;
  uint32_t ulRenderType = RT_VOID, ulPhysicsFlags = 0, ulCollisionFlags = 0, ulFlags = 0;
  int32_t iBrush = -1;
  bool bHasParent = false;
  int32_t iParent = -1;
  Placement plRelative;
  std::vector<OutProperty> aprops;
  bool bHasLight = false;
  std::vector<LightLayerRef> alayers;
  float fHealth = 0;
  uint32_t ulSpawnFlags = 0xFFFFFFFF;   // the TFE flags; the alpha can't save them (see SpawnFlagsText)
};

struct OutWorld {
  const World *pwoSource = nullptr;     // brushes and world settings
  int32_t iBackgroundViewer = -1;
  std::vector<OutEntity> aentities;
};

// tfe_read.cpp
float MipFactorToDistance(float fFactor);
void ReadTfeWorld(const Bytes &ab, const TfeClasses &tfe, World &wo, Report &rep);

// convert.cpp: maps classes, properties, textures and table indices;
// changes the brushes of wo in place (textures, flags, shadow layers)
void ConvertWorld(World &wo, const TfeClasses &tfe, AlphaClasses &alpha, const Options &opt,
                  OutWorld &out, Report &rep);

// alpha_write.cpp
Bytes WriteAlphaWorld(const OutWorld &out);

// The spawn flags file that AlphaFixes reads next to a level ("<level>.spawnflags"):
// the alpha's level format can't hold entities' spawn flags.  One line per entity
// whose flags aren't "exists always": index, class name, position, flags.
struct SpawnFlagsEntry {
  int32_t iEntity;
  std::string strClass;
  float af[3];
  uint32_t ulFlags;
};
std::string SpawnFlagsText(const std::string &strWorld, int32_t ctEntities, const std::vector<SpawnFlagsEntry> &aEntries);
constexpr uint32_t SPF_ALWAYS = 0x0007000F;   // every difficulty and game mode

// engine.cpp: the alpha's Engine.dll, running in this process (only when the
// converter runs from the alpha's Bin folder)
class AlphaEngine {
public:
  bool Start(const std::string &strGameDir, std::string &strError);
  // whether the engine can load this model or texture (relative to the game folder)
  bool CanLoad(const std::string &strRelFile);
  // the engine pass: load the converted world, initialize every entity again
  // (as if it had just been placed in the editor) and save it
  // aulSpawnFlags: the flags of the converted world's entities, in its order; they are
  // given to the loaded entities, and strSpawnFlags gets the saved world's spawn flags file
  bool Reinitialize(const std::string &strRelIn, const std::string &strRelOut, const std::vector<uint32_t> &aulSpawnFlags,
                    std::string &strSpawnFlags, std::string &strError);
  // a file to delete if the process has to be ended because the engine got stuck
  void CleanUpOnAbort(const std::string &strFile);
  // what the engine would have shown in message boxes
  const std::vector<std::string> &Messages();
};
