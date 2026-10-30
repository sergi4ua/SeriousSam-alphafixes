// Pause in the menu.
//
// Esc brings up the alpha's menu over the whole screen, but the game goes on
// behind it: the enemies keep fighting a player who can't see them.  The
// alpha's only pause is the Pause key (SeriousSam.exe 0x4233E5 calls
// CNetworkLibrary::TogglePause).  The First Encounter pauses a single-player
// game while its menu is up (UpdatePauseState in its SeriousSam.cpp, with a
// local pause the alpha engine doesn't have).
//
// The alpha's pause goes through the network: TogglePause sets the session
// state's "want pause" flag (CSessionState +0x4C), and every frame until the
// answer comes the session state sends the server a pause request.  For each
// request the server sets its own pause flag (CServer +0x20; while it's set
// the server makes no game ticks) and puts a pause block in the game stream,
// which every computer in the game applies: the session state's pause flag
// (+0x48), "Paused by '<player>'" in the console.  The server runs 20 times a
// second and sends the newest few blocks of the stream each time, so in the
// menu, which draws hundreds of frames a second, the requests pile up, blocks
// go unsent, and the console fills with "Missing sequence" and "Resending
// sequence" lines.
//
// So the mod pauses only games whose players are all on this computer (single
// player and split screen), and sets those three flags itself, as the
// round trip would, under the lock the server runs under.  Before each
// frame's game loop (SeriousSam.exe DoGame, 0x422C00) the menu coming up over
// such a game pauses it, and the menu going away resumes it.  A game paused
// with the Pause key before the menu came up stays paused.
#include "alphafixes.h"

namespace {

// SeriousSam.exe (13 Jan 2000)
constexpr uintptr_t EXE_DOGAME = 0x422C00;                 // void DoGame(void): game loop and drawing, every frame
const int *const _pbGameOn = (const int *)0x441170;        // a game is running
const int *const _pbMenuActive = (const int *)0x441178;    // the menu is up

// Engine.dll (alpha build)
constexpr size_t NET_IS_SERVER = 0x10;                     // CNetworkLibrary: this computer runs the server
constexpr size_t NET_SERVER = 0x14;                        // CServer *, made with the library
constexpr size_t NET_LOCK = 0x18;                          // CTCriticalSection the timer thread runs the server under
constexpr size_t NET_SESSION_STATE = 0x30;                 // CSessionState *
constexpr size_t NET_LOCAL_PLAYERS = 0x34;                 // CStaticArray<CPlayerSource>: count, array
constexpr size_t NET_PLAYING_DEMO = 0xB4;
constexpr size_t PLS_SIZE = 0x78, PLS_INDEX = 0x00, PLS_ACTIVE = 0x04;   // CPlayerSource
constexpr size_t SES_PLAYERS = 0x00;                       // CStaticArray<CPlayerTarget>: count, array
constexpr size_t PLT_SIZE = 0x38, PLT_ACTIVE = 0x00;       // CPlayerTarget
constexpr size_t SES_PAUSE = 0x48, SES_WANT_PAUSE = 0x4C;  // CSessionState
constexpr size_t SRV_PAUSE = 0x20;                         // CServer

using DoGame_t = void(__cdecl *)();
using SingleLockCtor_t = void(__fastcall *)(void *psl, void *pvEdx, void *pcs, BOOL bLock);
using SingleLockDtor_t = void(__fastcall *)(void *psl, void *pvEdx);

DoGame_t _pfnDoGame = nullptr;
char **_ppNetwork = nullptr;
SingleLockCtor_t _pfnSingleLockCtor = nullptr;
SingleLockDtor_t _pfnSingleLockDtor = nullptr;

bool _bMenuWasUp = false;   // the last frame had the menu up over a game the mod may pause
bool _bMenuPause = false;   // the mod paused the game for the menu

// This computer runs the game and has every player in it.
bool AllPlayersLocal(char *pnet, char *pses)
{
  if (!*(int *)(pnet + NET_IS_SERVER) || *(int *)(pnet + NET_PLAYING_DEMO)) return false;
  int ctPlayers = 0;
  int ctTargets = *(int *)(pses + SES_PLAYERS);
  char *aplt = *(char **)(pses + SES_PLAYERS + 4);
  for (int i = 0; aplt != nullptr && i < ctTargets; i++) {
    if (*(int *)(aplt + i * PLT_SIZE + PLT_ACTIVE)) ctPlayers++;
  }
  int ctLocal = 0;
  int ctSources = *(int *)(pnet + NET_LOCAL_PLAYERS);
  char *apls = *(char **)(pnet + NET_LOCAL_PLAYERS + 4);
  for (int i = 0; apls != nullptr && i < ctSources; i++) {
    char *pls = apls + i * PLS_SIZE;
    if (*(int *)(pls + PLS_ACTIVE) && *(int *)(pls + PLS_INDEX) >= 0) ctLocal++;
  }
  return ctPlayers > 0 && ctPlayers <= ctLocal;
}

// What the server's pause block would do, without the console message.
void SetPause(char *pnet, char *pses, bool bPause)
{
  void *pvLock = nullptr;   // CTSingleLock
  _pfnSingleLockCtor(&pvLock, nullptr, pnet + NET_LOCK, TRUE);
  char *psrv = *(char **)(pnet + NET_SERVER);
  if (psrv != nullptr) *(int *)(psrv + SRV_PAUSE) = bPause;
  *(int *)(pses + SES_PAUSE) = bPause;
  *(int *)(pses + SES_WANT_PAUSE) = bPause;
  _pfnSingleLockDtor(&pvLock, nullptr);
#if AF_TEST_AIDS
  using CurrentTick_t = float(__fastcall *)(void *ptm, void *pvEdx);
  auto ppTimer = (void **)EngineProc("?_pTimer@@3PAVCTimer@@A");
  auto pfnCurrentTick = (CurrentTick_t)EngineProc("?CurrentTick@CTimer@@QBE?BMXZ");
  if (ppTimer != nullptr && *ppTimer != nullptr && pfnCurrentTick != nullptr) {
    Log("Menu pause: %s, timer at %.2f s", bPause ? "paused" : "resumed", pfnCurrentTick(*ppTimer, nullptr));
  }
#endif
}

void UpdateMenuPause()
{
  char *pnet = *_ppNetwork;
  char *pses = pnet != nullptr ? *(char **)(pnet + NET_SESSION_STATE) : nullptr;
  if (pses == nullptr) return;
  bool bMenuUp = g_cfg.bMenuPause && *_pbGameOn && *_pbMenuActive && AllPlayersLocal(pnet, pses);
  if (bMenuUp && !_bMenuWasUp) {
    if (!*(int *)(pses + SES_PAUSE) && !*(int *)(pses + SES_WANT_PAUSE)) {
      SetPause(pnet, pses, true);
      _bMenuPause = true;
    }
  } else if (!bMenuUp && _bMenuPause) {
    SetPause(pnet, pses, false);
    _bMenuPause = false;
  }
  _bMenuWasUp = bMenuUp;
}

void __cdecl DoGame_Hook()
{
  UpdateMenuPause();
  _pfnDoGame();
}

}  // namespace

void InstallMenuPause()
{
  // push ebp; mov ebp, esp; push -1; push 0x425D56
  static const BYTE aubDoGame[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x56, 0x5D, 0x42, 0x00 };
  _ppNetwork = (char **)EngineProc("?_pNetwork@@3PAVCNetworkLibrary@@A");
  _pfnSingleLockCtor = (SingleLockCtor_t)EngineProc("??0CTSingleLock@@QAE@PAVCTCriticalSection@@H@Z");
  _pfnSingleLockDtor = (SingleLockDtor_t)EngineProc("??1CTSingleLock@@QAE@XZ");
  bool bOk = _ppNetwork != nullptr && _pfnSingleLockCtor != nullptr && _pfnSingleLockDtor != nullptr
          && CodeMatches(EXE_DOGAME, aubDoGame, sizeof(aubDoGame))
          && HookFunction((void *)EXE_DOGAME, (void *)DoGame_Hook, (void **)&_pfnDoGame, "DoGame");
  Log("Pause in the menu: %s", bOk ? "installed" : "code differs, not installed");
}
