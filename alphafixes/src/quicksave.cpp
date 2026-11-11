// Quick save, quick load, and loading after death.
//
// The alpha can save and load games (CGame::SaveGame and CGame::LoadGame in
// Game.dll, used by its Load/Save menus), but has no quick save, and a dead
// player stays dead: clicking does nothing.
//  - [Gameplay] QuickSaveKey (F6) saves the single-player game to
//    SaveGame\QuickSave.sav; QuickLoadKey (F9) loads it.  The keys are read
//    between frames, while the game window has the focus and the settings
//    window is closed.
//  - When a new game starts (CGame::NewGame) or the level changes
//    (CNetworkLibrary::ChangeLevel_internal), the game is saved to
//    SaveGame\LevelStart.sav a few frames later, once the player is alive.
//  - With [Gameplay] DeathLoad on, a left click after the player died (and
//    the click that killed him let go of) loads the quick save if it was made
//    or loaded in this game (since the last new game), and otherwise the
//    level start: the level restarts with what the player brought into it.
// After a load the menu is closed and the game marked as a running
// single-player game, as the game's own Load menu does (SeriousSam.exe
// 0x40BDD1).  Saving and loading only happen in single-player games: not in
// network or split-screen games, and saving not in demos.
//
// The alpha can't load a game while none is running: its own Load menu then
// shows a black screen (CGame::LoadGame adds the local players from
// gm_aiStartLocalPlayers, which only a new game sets), and with them set up
// the game crashes drawing the first model (NaN shading, Engine.dll
// 0x60063298).  Loading while a game runs works, even a save from another
// session.  So with no game running, a quick load first starts a new
// single-player game on the save's level (the DFNM chunk near the start of the
// file), as the Single player menu does (0x40AD90), and then loads the save.
#include "alphafixes.h"

namespace {

// SeriousSam.exe (13 Jan 2000)
constexpr uintptr_t EXE_GAME_ON = 0x441170;             // a game is running
constexpr uintptr_t EXE_MENU_ACTIVE = 0x441178;         // the menu is up
constexpr uintptr_t EXE_GAME_TYPE = 0x43D304;           // 1 single player (NewGame "Local"), 4 demo
constexpr uintptr_t EXE_CURRENT_MENU = 0x43D308;        // CGameMenu *: vtable slot 3 ends the menu
constexpr uintptr_t EXE_INPUT_ON = 0x4411A0;            // the exe has called CInput::EnableInput
constexpr uintptr_t EXE_SET_INPUT_ON = 0x420DF9;        // mov [EXE_INPUT_ON], 1 right after that call
constexpr int GAME_SINGLE = 1, GAME_DEMO = 4;
constexpr size_t EN_HEALTH = 0xB0;                      // CLiveEntity::en_fHealth
// CGame (Game.dll): what SeriousSam.exe sets for a single-player game before NewGame (0x40ADB7)
constexpr size_t GM_SINGLEPLAYER = 0xF98;               // the player profile chosen for single player
constexpr size_t GM_STARTSPLITCFG = 0xFA4;              // split-screen configuration: 0 one player
constexpr size_t GM_PROVIDER = 0xFDC;                   // network provider (CTString)
constexpr size_t GM_STARTLOCALPLAYERS = 0xFF8;          // the four local players, -1 none

const char STR_QUICKSAVE[] = "SaveGame\\QuickSave.sav";
const char STR_LEVELSTART[] = "SaveGame\\LevelStart.sav";

struct EngFileName { char *str; long lReserved; };      // CTFileName: the string and one more long
using FileNameCtor_t = void(__fastcall *)(EngFileName *pfnm, void *pvEdx, const char *str, int iOffset);
using FileNameDtor_t = void(__fastcall *)(EngFileName *pfnm, void *pvEdx);
using GameFile_t = BOOL(__fastcall *)(void *pGame, void *pvEdx, const EngFileName *pfnm);
using NewGame_t = BOOL(__fastcall *)(void *pGame, void *pvEdx, const void *pstrSession, const void *pfnmWorld, void *psp);
using Method_t = void(__fastcall *)(void *pThis, void *pvEdx);
using GetPlayerEntity_t = char **(__cdecl *)(char **ppenResult, long iPlayer);   // returns a CEntityPointer
using CPrintF_t = void(__cdecl *)(const char *strFormat, ...);
using StringDuplicate_t = char *(__cdecl *)(const char *str);
using FreeMemory_t = void(__cdecl *)(void *pv);
using SetSession_t = void(__fastcall *)(void *pGame, void *pvEdx, void *psp);

FileNameCtor_t _pfnFileNameCtor = nullptr;
FileNameDtor_t _pfnFileNameDtor = nullptr;
GameFile_t _pfnSaveGame = nullptr, _pfnLoadGame = nullptr;
NewGame_t _pfnNewGame = nullptr;
Method_t _pfnChangeLevel = nullptr, _pfnRemReference = nullptr;
GetPlayerEntity_t _pfnGetPlayerEntity = nullptr;
CPrintF_t _pfnCPrintF = nullptr;
StringDuplicate_t _pfnStringDuplicate = nullptr;
FreeMemory_t _pfnFreeMemory = nullptr;
SetSession_t _pfnSetSinglePlayerSession = nullptr;
void **_ppGame = nullptr;
bool _bInstalled = false;

bool _bLevelStartPending = false;
int _ctPendingFrames = 0;
bool _bQuickSaveThisGame = false;   // the quick save belongs to the game being played
DWORD _tmDied = 0;                  // when the player was first seen dead, 0 while alive
bool _bWaitRelease = false;         // the button that was down at death hasn't been let go yet
bool _abWasDown[3] = {};

int GameType() { return *(int *)EXE_GAME_TYPE; }
bool GameOn() { return *(int *)EXE_GAME_ON != 0; }
bool MenuUp() { return *(int *)EXE_MENU_ACTIVE != 0; }
bool InSinglePlayerGame() { return GameOn() && !MenuUp() && GameType() == GAME_SINGLE; }

std::string RootDir()
{
  std::string str = GameDir();   // ...\Bin\  ->  ...\  (the game's paths are relative to its folder)
  if (str.size() > 4 && _stricmp(str.c_str() + str.size() - 4, "Bin\\") == 0) str.resize(str.size() - 4);
  return str;
}

bool Exists(const char *strRel) { return GetFileAttributesA((RootDir() + strRel).c_str()) != INVALID_FILE_ATTRIBUTES; }

// health of player 0, or false if there is none
bool PlayerHealth(float &fHealth)
{
  char *pen = nullptr;
  _pfnGetPlayerEntity(&pen, 0);
  if (pen == nullptr) return false;
  fHealth = *(float *)(pen + EN_HEALTH);
  _pfnRemReference(pen, nullptr);
  return true;
}

bool CallGameFile(GameFile_t pfn, const char *strRel)
{
  EngFileName fnm;
  _pfnFileNameCtor(&fnm, nullptr, strRel, 0);
  BOOL b = pfn(*_ppGame, nullptr, &fnm);
  _pfnFileNameDtor(&fnm, nullptr);
  return b != 0;
}

bool Save(const char *strRel, const char *strWhat)
{
  CreateDirectoryA((RootDir() + "SaveGame").c_str(), nullptr);
  bool bOk = CallGameFile(_pfnSaveGame, strRel);
  _pfnCPrintF(bOk ? "%s saved\n" : "%s: saving failed\n", strWhat);
  Log("%s: %s %s", strWhat, bOk ? "saved to" : "saving failed,", strRel);
  return bOk;
}

// a local single-player game, as the game's Single player menu sets it up; loading from the main menu
// (or a demo) needs it, in a single-player game it is already so
void SetUpSinglePlayer()
{
  char *pGame = (char *)*_ppGame;
  *(int *)(pGame + GM_STARTSPLITCFG) = 0;
  int *piPlayers = (int *)(pGame + GM_STARTLOCALPLAYERS);
  piPlayers[0] = *(int *)(pGame + GM_SINGLEPLAYER);
  piPlayers[1] = piPlayers[2] = piPlayers[3] = -1;
  char **pstrProvider = (char **)(pGame + GM_PROVIDER);
  char *strOld = *pstrProvider;
  *pstrProvider = _pfnStringDuplicate("Local");
  if (strOld != nullptr) _pfnFreeMemory(strOld);
}

// Loading stops the running game, and CGame::StopGame (Game.dll 0x602C4B60) disables the input.
// The exe enables it again only when its own flag says it is off (0x420D70, run every frame; the
// flag changes when the menu or the console opens or closes).  A load from the menu leaves the flag
// off, but a quick load in a game leaves it on, and the game then gets no keys or mouse until the
// menu or console is opened.  So the flag follows CInput::inp_bInputEnabled after a load.
bool _bInputFlagOk = false;
void **_ppInput = nullptr;

void ResyncInput()
{
  if (!_bInputFlagOk || _ppInput == nullptr || *_ppInput == nullptr) return;
  if (*(int *)*_ppInput == 0 && *(int *)EXE_INPUT_ON != 0) {
    *(int *)EXE_INPUT_ON = 0;   // the exe enables the input again on the next frame
    Log("Quick load: input enabled again");
  }
}

// as the game's menus do after starting or loading a game
void EnterGame()
{
  if (MenuUp()) {
    void *pMenu = *(void **)EXE_CURRENT_MENU;
    if (pMenu != nullptr) ((Method_t)(*(void ***)pMenu)[3])(pMenu, nullptr);
  }
  *(int *)EXE_MENU_ACTIVE = 0;
  *(int *)EXE_GAME_TYPE = GAME_SINGLE;
  *(int *)EXE_GAME_ON = 1;
  ResyncInput();
}

// the level a save was made in: its DFNM chunk (length, then the file name)
std::string SaveWorld(const char *strRel)
{
  std::string strWorld;
  HANDLE h = CreateFileA((RootDir() + strRel).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return strWorld;
  static char ach[8192];
  DWORD ct = 0;
  ReadFile(h, ach, sizeof(ach), &ct, nullptr);
  CloseHandle(h);
  for (DWORD i = 0; i + 8 < ct; i++) {
    if (memcmp(ach + i, "DFNM", 4) != 0) continue;
    int iLen = *(int *)(ach + i + 4);
    if (iLen > 4 && iLen < 260 && i + 8 + iLen <= ct) strWorld.assign(ach + i + 8, iLen);
    break;
  }
  if (strWorld.size() < 5 || _stricmp(strWorld.c_str() + strWorld.size() - 4, ".wld") != 0) strWorld.clear();
  return strWorld;
}

// a new single-player game on that level, as the Single player menu starts one
bool StartGameOn(const std::string &strWorld)
{
  SetUpSinglePlayer();
  static BYTE aubSession[8192];   // CSessionProperties; the game's own is smaller
  memset(aubSession, 0, sizeof(aubSession));
  _pfnSetSinglePlayerSession(*_ppGame, nullptr, aubSession);
  EngFileName fnm;
  _pfnFileNameCtor(&fnm, nullptr, strWorld.c_str(), 0);
  BOOL b = _pfnNewGame(*_ppGame, nullptr, &fnm, &fnm, aubSession);   // the session is named after the level, as there
  _pfnFileNameDtor(&fnm, nullptr);
  Log("Quick load: started a game on %s first: %s", strWorld.c_str(), b ? "ok" : "failed");
  if (b) EnterGame();
  return b != 0;
}

bool Load(const char *strRel, const char *strWhat)
{
  if (!GameOn() || GameType() != GAME_SINGLE) {
    std::string strWorld = SaveWorld(strRel);
    if (strWorld.empty() || !StartGameOn(strWorld)) {
      _pfnCPrintF("%s: can't start its level\n", strWhat);
      return false;
    }
  }
  SetUpSinglePlayer();
  bool bOk = CallGameFile(_pfnLoadGame, strRel);
  if (bOk) {
    EnterGame();
  } else {
    *(int *)EXE_GAME_ON = 0;
  }
  _pfnCPrintF(bOk ? "%s loaded\n" : "%s: loading failed\n", strWhat);
  Log("%s: %s %s", strWhat, bOk ? "loaded" : "loading failed,", strRel);
  _tmDied = 0;
  _bLevelStartPending = false;
  return bOk;
}

void QuickSave()
{
  float fHealth;
  if (!InSinglePlayerGame() || !PlayerHealth(fHealth) || fHealth <= 0.0f) return;
  if (Save(STR_QUICKSAVE, "Quick save")) _bQuickSaveThisGame = true;
}

void QuickLoad()
{
  int iType = GameType();
  if (GameOn() && iType != GAME_SINGLE && iType != GAME_DEMO) return;   // not in network or split-screen games
  if (!Exists(STR_QUICKSAVE)) {
    _pfnCPrintF("No quick save\n");
    return;
  }
  if (Load(STR_QUICKSAVE, "Quick save")) _bQuickSaveThisGame = true;
}

// after death: the quick save of this game, or else the start of the level
void LoadAfterDeath()
{
  if (_bQuickSaveThisGame && Exists(STR_QUICKSAVE)) {
    Load(STR_QUICKSAVE, "Quick save");
  } else if (Exists(STR_LEVELSTART)) {
    Load(STR_LEVELSTART, "Level start");
  } else {
    _pfnCPrintF("No save to load\n");
    _tmDied = 0;
  }
}

// the key went down since the last frame
bool Pressed(int i, int vk, bool bActive)
{
  bool bDown = bActive && (GetAsyncKeyState(vk) & 0x8000) != 0;
  bool bPressed = bDown && !_abWasDown[i];
  _abWasDown[i] = bDown;
  return bPressed;
}

BOOL __fastcall NewGame_Hook(void *pGame, void *pvEdx, const void *pstrSession, const void *pfnmWorld, void *psp)
{
  BOOL b = _pfnNewGame(pGame, pvEdx, pstrSession, pfnmWorld, psp);
  if (b) {
    ApplyDifficulty("new game");
    _bLevelStartPending = true;
    _ctPendingFrames = 0;
    _bQuickSaveThisGame = false;
    _tmDied = 0;
  }
  return b;
}

void __fastcall ChangeLevel_Hook(void *pnet, void *pvEdx)
{
  _pfnChangeLevel(pnet, pvEdx);
  ApplyDifficulty("level change");
  _bLevelStartPending = true;
  _ctPendingFrames = 0;
  _tmDied = 0;
}

}  // namespace

// Main thread, between frames.
void RunQuickSave()
{
  if (!_bInstalled || *_ppGame == nullptr) return;
  HWND hwnd = GetGameWindow();
  bool bActive = hwnd != nullptr && GetForegroundWindow() == hwnd && !IsOverlayVisible();
  bool bSave = Pressed(0, g_cfg.iQuickSaveKey, bActive);
  bool bLoad = Pressed(1, g_cfg.iQuickLoadKey, bActive);
  bool bClick = Pressed(2, VK_LBUTTON, bActive);

  if (bSave) QuickSave();
  if (bLoad) { QuickLoad(); return; }

  float fHealth = 1.0f;
  bool bPlayer = InSinglePlayerGame() && PlayerHealth(fHealth);

  // the level-start save, once the new level is running with the player alive
  if (_bLevelStartPending && bPlayer && fHealth > 0.0f && ++_ctPendingFrames >= 3) {
    _bLevelStartPending = false;
    Save(STR_LEVELSTART, "Level start");
  }

  // death: a click (pressed after death, half a second on) loads
  if (!bPlayer || fHealth > 0.0f) {
    _tmDied = 0;
    return;
  }
  if (_tmDied == 0) {
    _tmDied = GetTickCount();
    _bWaitRelease = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    return;
  }
  if (_bWaitRelease) {
    _bWaitRelease = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    return;
  }
  if (g_cfg.bDeathLoad && bClick && GetTickCount() - _tmDied > 500) LoadAfterDeath();
}

void InstallQuickSave()
{
  HMODULE hGame = GetModuleHandleA("Game.dll");
  _pfnFileNameCtor = (FileNameCtor_t)EngineProc("??0CTFileName@@QAE@PBDH@Z");
  _pfnFileNameDtor = (FileNameDtor_t)EngineProc("??1CTFileName@@QAE@XZ");
  _pfnRemReference = (Method_t)EngineProc("?RemReference@CEntity@@QAEXXZ");
  _pfnGetPlayerEntity = (GetPlayerEntity_t)EngineProc("?GetPlayerEntity@CEntity@@SA?AVCEntityPointer@@J@Z");
  _pfnCPrintF = (CPrintF_t)EngineProc("?CPrintF@@YAXPBDZZ");
  _pfnStringDuplicate = (StringDuplicate_t)EngineProc("?StringDuplicate@@YAPADPBD@Z");
  _pfnFreeMemory = (FreeMemory_t)EngineProc("?FreeMemory@@YAXPAX@Z");
  _ppInput = (void **)EngineProc("?_pInput@@3PAVCInput@@A");
  static const BYTE aubSetInputOn[] = { 0xC7, 0x05, 0xA0, 0x11, 0x44, 0x00, 0x01, 0x00, 0x00, 0x00 };
  _bInputFlagOk = CodeMatches(EXE_SET_INPUT_ON, aubSetInputOn, sizeof(aubSetInputOn));
  if (!_bInputFlagOk) Log("Quick save: the exe's input flag isn't where expected; loads may leave the input off");
  _pfnSetSinglePlayerSession = (SetSession_t)GetProcAddress(hGame, "?SetSinglePlayerSession@CGame@@QAEXAAVCSessionProperties@@@Z");
  _pfnSaveGame = (GameFile_t)GetProcAddress(hGame, "?SaveGame@CGame@@QAEHABVCTFileName@@@Z");
  _pfnLoadGame = (GameFile_t)GetProcAddress(hGame, "?LoadGame@CGame@@QAEHABVCTFileName@@@Z");
  _ppGame = (void **)GetProcAddress(hGame, "?_pGame@@3PAVCGame@@A");
  bool bOk = _pfnFileNameCtor && _pfnFileNameDtor && _pfnRemReference && _pfnGetPlayerEntity && _pfnCPrintF
          && _pfnStringDuplicate && _pfnFreeMemory && _pfnSetSinglePlayerSession
          && _pfnSaveGame && _pfnLoadGame && _ppGame
          && HookFunction(GetProcAddress(hGame, "?NewGame@CGame@@QAEHABVCTString@@ABVCTFileName@@AAVCSessionProperties@@@Z"),
                          (void *)NewGame_Hook, (void **)&_pfnNewGame, "CGame::NewGame")
          && HookFunction(EngineProc("?ChangeLevel_internal@CNetworkLibrary@@QAEXXZ"), (void *)ChangeLevel_Hook,
                          (void **)&_pfnChangeLevel, "CNetworkLibrary::ChangeLevel_internal");
  _bInstalled = bOk;
  std::string strSaveKey = KeyName(g_cfg.iQuickSaveKey);   // KeyName() has one buffer
  Log("Quick save: %s (%s saves, %s loads)", bOk ? "installed" : "failed", strSaveKey.c_str(), KeyName(g_cfg.iQuickLoadKey));
}
