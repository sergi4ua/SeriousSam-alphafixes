// Difficulty: entities' spawn flags are kept and take effect.
//
// The World Editor gives every entity spawn flags (Easy, Normal, Hard,
// Extreme; Single, Cooperative, Deathmatch), but the alpha's level format has
// no room for them: CEntity::Write_t and Read_t store the render type, physics,
// collision and entity flags only, and the CEntity constructor sets the spawn
// flags to 0xFFFFFFFF.  So the editor's choice was lost on saving, every
// entity of every level exists on every difficulty, and nothing in the game
// reads the flags anyway.  The alpha has no difficulty either (its
// single-player session has no such field, and the menu's Easy/Medium/Hard
// page starts the same game for all three).
//
// Spawn flags file.  After CWorld::Save_t (the World Editor's saves), the mod
// writes "<level>.spawnflags" next to the level, listing the entities whose
// flags aren't "exists always": their index in the world's entity list, class
// and position (to check them), and flags.  With no such entity, an old file
// is deleted.  After CWorld::Load_t (opening a level in the editor, a new game
// or a level change in the game), the file's flags are given back to the
// entities, but only if the level has the same number of entities and every
// listed entity has the same class and position; otherwise the file is
// ignored and the log says why.  The .wld itself stays in the alpha's format.
// WldConvert writes the same file for converted First Encounter levels.
//
// Difficulty.  With [Gameplay] Difficulty set, each newly loaded level (a new
// game or a level change; not a loaded save, which already holds its
// entities) loses the entities whose spawn flags leave out the chosen
// difficulty, as in the released game.  The default is Normal; the alpha's own
// levels have no flags, so it changes nothing there.  The game-mode flags are
// left alone.  Players and brushes are never removed (a brush is the level's
// geometry, which the engine links up while loading).  Enemies keep their health and damage: the
// alpha's enemies have nothing that depends on difficulty.
//
// Spawn flag bits (the World Editor's entity info, 0x44AC8B): Easy 1, Normal 2,
// Hard 4, Extreme 8, Single 0x10000, Deathmatch 0x20000, Cooperative 0x40000;
// all seven set means "exists always".
#include "alphafixes.h"

#include <cstdio>
#include <cstdlib>

namespace {

// Engine.dll layout (alpha build)
constexpr size_t NET_WORLD = 0x944;            // CNetworkLibrary::ga_World
constexpr size_t WORLD_ENTITIES = 0x100;       // CWorld::wo_cenEntities: array, count
constexpr size_t EN_RENDERTYPE = 0x04;
constexpr size_t EN_FLAGS = 0x10;
constexpr size_t EN_SPAWNFLAGS = 0x14;
constexpr size_t EN_REFERENCES = 0x18;
constexpr size_t EN_PLACEMENT = 0x1C;          // position x, y, z, then angles
constexpr size_t EN_CLASS = 0x58;              // CEntity::en_pecClass
constexpr size_t EC_DLLCLASS = 0x18;           // CEntityClass::ec_pdecDLLClass
constexpr size_t DEC_NAME = 0x18;              // CDLLEntityClass::dec_strName
constexpr ULONG ENF_DELETED = 1UL << 2;
constexpr int RT_BRUSH = 4, RT_FIELDBRUSH = 8;

constexpr ULONG SPF_DIFFICULTY = 0x0000000F;
constexpr ULONG SPF_ALWAYS = 0x0007000F;       // every difficulty and game mode

using Method_t = void(__fastcall *)(void *pThis, void *pvEdx);
using WorldFile_t = void(__fastcall *)(void *pwo, void *pvEdx, const void *pfnm);

char **_ppNetwork = nullptr;
const char *const *_pstrAppPath = nullptr;     // _fnmApplicationPath, with a trailing backslash
Method_t _pfnDestroy = nullptr;
Method_t _pfnRemReference = nullptr;
WorldFile_t _pfnWorldSave = nullptr, _pfnWorldLoad = nullptr;
bool _bInstalled = false;

const char *const _astrDifficulty[] = { "off", "Easy", "Normal", "Hard", "Extreme" };
const char STR_HEADER[] = "AlphaFixes spawn flags 1";

const char *ClassName(char *pen)
{
  char *pec = *(char **)(pen + EN_CLASS);
  char *pdec = pec != nullptr ? *(char **)(pec + EC_DLLCLASS) : nullptr;
  const char *str = pdec != nullptr ? *(const char **)(pdec + DEC_NAME) : nullptr;
  return str != nullptr ? str : "?";
}

const float *Position(char *pen) { return (const float *)(pen + EN_PLACEMENT); }

bool Entities(char *pwo, char **&apen, int &ct)
{
  apen = *(char ***)(pwo + WORLD_ENTITIES);
  ct = *(int *)(pwo + WORLD_ENTITIES + 4);
  return apen != nullptr && ct > 0;
}

// "Levels\X.wld" -> "<game>\Levels\X.spawnflags"; strShort gets "Levels\X.spawnflags"
std::string SpawnFilePath(const void *pfnm, std::string &strShort)
{
  const char *strWorld = *(const char *const *)pfnm;
  if (strWorld == nullptr || strWorld[0] == 0) return std::string();
  std::string str = strWorld;
  size_t iDot = str.find_last_of('.');
  size_t iSlash = str.find_last_of("\\/");
  if (iDot != std::string::npos && (iSlash == std::string::npos || iDot > iSlash)) str.erase(iDot);
  str += ".spawnflags";
  strShort = str;
  bool bAbsolute = str.size() > 1 && (str[1] == ':' || (str[0] == '\\' && str[1] == '\\'));
  if (bAbsolute || _pstrAppPath == nullptr || *_pstrAppPath == nullptr) return str;
  return std::string(*_pstrAppPath) + str;
}

void WriteSpawnFile(char *pwo, const void *pfnm)
{
  std::string strShort, strPath = SpawnFilePath(pfnm, strShort);
  if (strPath.empty()) return;
  char **apen;
  int ct;
  std::vector<int> aiFlagged;
  if (Entities(pwo, apen, ct)) {
    for (int i = 0; i < ct; i++) {
      if (apen[i] != nullptr && (*(ULONG *)(apen[i] + EN_SPAWNFLAGS) & SPF_ALWAYS) != SPF_ALWAYS) aiFlagged.push_back(i);
    }
  }
  if (aiFlagged.empty()) {
    if (DeleteFileA(strPath.c_str())) Log("Spawn flags: no entity has any, deleted %s", strShort.c_str());
    return;
  }
  FILE *pf = fopen(strPath.c_str(), "w");
  if (pf == nullptr) {
    Log("Spawn flags: can't write %s", strShort.c_str());
    return;
  }
  const char *strWorld = *(const char *const *)pfnm;
  fprintf(pf, "%s\n", STR_HEADER);
  fprintf(pf, "; The spawn flags of %s, which the alpha's level format can't hold.\n", strWorld);
  fprintf(pf, "; Written by AlphaFixes when the level is saved; read when it is loaded.\n");
  fprintf(pf, "; Flags: Easy 1, Normal 2, Hard 4, Extreme 8, Single 10000, Deathmatch 20000, Cooperative 40000.\n");
  fprintf(pf, "; entity index, class, position x y z (to check the entity), flags (hex)\n");
  fprintf(pf, "Entities %d\n", ct);
  for (int i : aiFlagged) {
    const float *pv = Position(apen[i]);
    fprintf(pf, "%d \"%s\" %.9g %.9g %.9g %08lX\n", i, ClassName(apen[i]), pv[0], pv[1], pv[2],
            *(ULONG *)(apen[i] + EN_SPAWNFLAGS));
  }
  bool bOk = ferror(pf) == 0;
  bOk = fclose(pf) == 0 && bOk;
  Log("Spawn flags: %s %s (%d of %d entities)", bOk ? "wrote" : "failed writing", strShort.c_str(), (int)aiFlagged.size(), ct);
}

struct SpawnEntry {
  int iEntity;
  std::string strClass;
  float af[3];
  ULONG ulFlags;
};

void ReadSpawnFile(char *pwo, const void *pfnm)
{
  std::string strShort, strPath = SpawnFilePath(pfnm, strShort);
  if (strPath.empty()) return;
  FILE *pf = fopen(strPath.c_str(), "r");
  if (pf == nullptr) return;

  char **apen;
  int ct = 0;
  bool bEntities = Entities(pwo, apen, ct);
  std::vector<SpawnEntry> aEntries;
  int ctInFile = -1;
  std::string strProblem;
  char strLine[1024];
  int iLine = 0;
  while (strProblem.empty() && fgets(strLine, sizeof(strLine), pf) != nullptr) {
    iLine++;
    size_t ctLen = strlen(strLine);
    while (ctLen > 0 && (strLine[ctLen - 1] == '\n' || strLine[ctLen - 1] == '\r' || strLine[ctLen - 1] == ' ')) strLine[--ctLen] = 0;
    if (iLine == 1) {
      if (strcmp(strLine, STR_HEADER) != 0) strProblem = "not a spawn flags file";
      continue;
    }
    if (strLine[0] == 0 || strLine[0] == ';') continue;
    if (strncmp(strLine, "Entities ", 9) == 0) {
      ctInFile = atoi(strLine + 9);
      continue;
    }
    // index "class" x y z flags
    SpawnEntry se;
    char *pch = strLine;
    se.iEntity = (int)strtol(pch, &pch, 10);
    char *pchOpen = strchr(pch, '"');
    char *pchClose = pchOpen != nullptr ? strchr(pchOpen + 1, '"') : nullptr;
    if (pchClose == nullptr) {
      strProblem = "line " + std::to_string(iLine) + " can't be read";
      break;
    }
    se.strClass.assign(pchOpen + 1, pchClose);
    pch = pchClose + 1;
    for (float &f : se.af) f = strtof(pch, &pch);
    se.ulFlags = strtoul(pch, &pch, 16);
    aEntries.push_back(se);
  }
  fclose(pf);

  if (strProblem.empty() && !bEntities) strProblem = "the level has no entities";
  if (strProblem.empty() && ctInFile != ct) {
    strProblem = "it is for " + std::to_string(ctInFile) + " entities, the level has " + std::to_string(ct);
  }
  for (size_t i = 0; strProblem.empty() && i < aEntries.size(); i++) {
    const SpawnEntry &se = aEntries[i];
    char *pen = se.iEntity >= 0 && se.iEntity < ct ? apen[se.iEntity] : nullptr;
    const float *pv = pen != nullptr ? Position(pen) : nullptr;
    if (pen == nullptr || se.strClass != ClassName(pen) || pv[0] != se.af[0] || pv[1] != se.af[1] || pv[2] != se.af[2]) {
      strProblem = "entity " + std::to_string(se.iEntity) + " (" + se.strClass + ") isn't where the file says";
    }
  }
  if (!strProblem.empty()) {
    Log("Spawn flags: %s ignored, %s; the level was probably saved without AlphaFixes", strShort.c_str(), strProblem.c_str());
    return;
  }
  for (const SpawnEntry &se : aEntries) *(ULONG *)(apen[se.iEntity] + EN_SPAWNFLAGS) = se.ulFlags;
  Log("Spawn flags: read %s (%d of %d entities)", strShort.c_str(), (int)aEntries.size(), ct);
}

// Save_t and Load_t throw engine errors; the file is handled only once they return.
void __fastcall WorldSave_Hook(void *pwo, void *pvEdx, const void *pfnm)
{
  _pfnWorldSave(pwo, pvEdx, pfnm);
  WriteSpawnFile((char *)pwo, pfnm);
}

void __fastcall WorldLoad_Hook(void *pwo, void *pvEdx, const void *pfnm)
{
  _pfnWorldLoad(pwo, pvEdx, pfnm);
  ReadSpawnFile((char *)pwo, pfnm);
}

}  // namespace

const char *DifficultyName(int iDifficulty)
{
  return iDifficulty >= 0 && iDifficulty < DIFFICULTY_COUNT ? _astrDifficulty[iDifficulty] : "?";
}

// After a level is loaded fresh (main thread, before the game has run on it).
void ApplyDifficulty(const char *strWhen)
{
  if (!_bInstalled || *_ppNetwork == nullptr) return;
  char **apen;
  int ctEntities;
  if (!Entities(*_ppNetwork + NET_WORLD, apen, ctEntities)) return;

  // how many entities each difficulty leaves out, for the log
  int actMissing[DIFFICULTY_COUNT] = {};
  for (int i = 0; i < ctEntities; i++) {
    if (apen[i] == nullptr) continue;
    ULONG ulSpawn = *(ULONG *)(apen[i] + EN_SPAWNFLAGS);
    for (int iDiff = 1; iDiff < DIFFICULTY_COUNT; iDiff++) {
      if (!(ulSpawn & (1UL << (iDiff - 1)))) actMissing[iDiff]++;
    }
  }
  int iDifficulty = g_cfg.iDifficulty;
  if (iDifficulty <= DIFFICULTY_OFF || iDifficulty >= DIFFICULTY_COUNT) {
    Log("Difficulty off (%s): %d entities; Easy would leave out %d, Normal %d, Hard %d, Extreme %d", strWhen, ctEntities,
        actMissing[1], actMissing[2], actMissing[3], actMissing[4]);
    return;
  }

  // collect first: destroying changes the world's entity list
  ULONG ulWanted = 1UL << (iDifficulty - 1);
  std::vector<char *> apenRemove;
  for (int i = 0; i < ctEntities; i++) {
    char *pen = apen[i];
    if (pen == nullptr || (*(ULONG *)(pen + EN_FLAGS) & ENF_DELETED)) continue;
    if (*(ULONG *)(pen + EN_SPAWNFLAGS) & ulWanted) continue;
    if (strcmp(ClassName(pen), "Player") == 0) continue;
    int iRenderType = *(int *)(pen + EN_RENDERTYPE);
    if (iRenderType == RT_BRUSH || iRenderType == RT_FIELDBRUSH) {
      Log("Difficulty: kept %s, a brush (spawn flags %08lX)", ClassName(pen), *(ULONG *)(pen + EN_SPAWNFLAGS));
      continue;
    }
    apenRemove.push_back(pen);
  }
  // hold a reference to each, so none is freed while the others are destroyed
  for (char *pen : apenRemove) (*(int *)(pen + EN_REFERENCES))++;
  for (char *pen : apenRemove) {
    const float *pv = Position(pen);
    Log("Difficulty: removed %s at %.1f %.1f %.1f (spawn flags %08lX)", ClassName(pen), pv[0], pv[1], pv[2],
        *(ULONG *)(pen + EN_SPAWNFLAGS));
    _pfnDestroy(pen, nullptr);
  }
  for (char *pen : apenRemove) _pfnRemReference(pen, nullptr);
  Log("Difficulty %s (%s): removed %d of %d entities", DifficultyName(iDifficulty), strWhen, (int)apenRemove.size(),
      ctEntities);
}

// The game reads spawn flags files and removes entities; the World Editor also writes the files.
void InstallSpawnFlags(bool bEditor)
{
  _pstrAppPath = (const char *const *)EngineProc("?_fnmApplicationPath@@3VCTFileName@@A");
  bool bOk = _pstrAppPath != nullptr
          && HookFunction(EngineProc("?Load_t@CWorld@@QAEXABVCTFileName@@@Z"), (void *)WorldLoad_Hook,
                          (void **)&_pfnWorldLoad, "CWorld::Load_t");
  if (bOk && bEditor) {
    bOk = HookFunction(EngineProc("?Save_t@CWorld@@QAEXABVCTFileName@@@Z"), (void *)WorldSave_Hook,
                       (void **)&_pfnWorldSave, "CWorld::Save_t");
  }
  Log("Spawn flags files: %s", bOk ? (bEditor ? "read and written" : "read") : "failed");
  if (bEditor) return;

  _ppNetwork = (char **)EngineProc("?_pNetwork@@3PAVCNetworkLibrary@@A");
  _pfnDestroy = (Method_t)EngineProc("?Destroy@CEntity@@QAEXXZ");
  _pfnRemReference = (Method_t)EngineProc("?RemReference@CEntity@@QAEXXZ");
  _bInstalled = _ppNetwork != nullptr && _pfnDestroy != nullptr && _pfnRemReference != nullptr;
  Log("Difficulty: %s (%s)", _bInstalled ? "installed" : "failed", DifficultyName(g_cfg.iDifficulty));
}
