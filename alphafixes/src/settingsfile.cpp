// The game's own settings file.
//
// The game and the World Editor both keep their persistent console variables
// in Scripts\PersistentSymbols.scr.  The editor rewrites the file without the
// game's sam_* variables (resolution, fullscreen, driver, sound), so after an
// editor session the game starts with defaults.  Game.dll reads and writes the
// file through a global file name (CTFileName at 0x602D2760, set by its static
// initializer); the mod points it at Scripts\PersistentSymbols_Game.scr, in the
// game's process only.  The first time, the shared file is copied over.
#include "alphafixes.h"

namespace {

// Game.dll (alpha build)
constexpr uintptr_t GAME_FNM_PERSISTENT = 0x602D2760;   // CTFileName: its char *
constexpr char STR_SHARED[] = "Scripts\\PersistentSymbols.scr";
constexpr char STR_OWN[] = "Scripts\\PersistentSymbols_Game.scr";

}  // namespace

void InstallGameSettingsFile()
{
  if (!g_cfg.bOwnSettingsFile) return;
  using StringDuplicate_t = char *(__cdecl *)(const char *);
  using FreeMemory_t = void(__cdecl *)(void *);
  auto pfnStringDuplicate = (StringDuplicate_t)EngineProc("?StringDuplicate@@YAPADPBD@Z");
  auto pfnFreeMemory = (FreeMemory_t)EngineProc("?FreeMemory@@YAXPAX@Z");
  char **ppstr = (char **)GAME_FNM_PERSISTENT;
  if (pfnStringDuplicate == nullptr || pfnFreeMemory == nullptr || *ppstr == nullptr || strcmp(*ppstr, STR_SHARED) != 0) {
    Log("Settings file: Game.dll differs, the game keeps using %s", STR_SHARED);
    return;
  }

  std::string strRoot = GameDir() + "..\\";
  std::string strOwn = strRoot + STR_OWN, strShared = strRoot + STR_SHARED;
  if (GetFileAttributesA(strOwn.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesA(strShared.c_str()) != INVALID_FILE_ATTRIBUTES) {
    Log("Settings file: %s", CopyFileA(strShared.c_str(), strOwn.c_str(), TRUE) ? "copied the shared file" : "could not copy the shared file");
  }

  // the engine allocates and frees CTString memory itself
  char *strOld = *ppstr;
  *ppstr = pfnStringDuplicate(STR_OWN);
  pfnFreeMemory(strOld);
  Log("Settings file: %s", STR_OWN);
}
