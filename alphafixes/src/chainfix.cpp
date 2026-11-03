// Trigger messages with no player in the triggering chain.
//
// A Trigger's message (and its "Count tell" countdown) goes through
// Entities.dll's PrintCenterMessage (0x601A6620), which sends it to the
// entity that started the chain: the player who touched a field, pressed a
// switch and so on.  When that entity is missing, the alpha only prints
// "Cannot print message (...), triggering chain broken" to the console and the
// message is never shown.  That happens for a Trigger with Auto start, one
// started by a Start event, and every Trigger behind a WatchPlayers: the
// alpha's WatchPlayers always sends its events with no entity at all
// (SendToTarget(owner, event, NULL)).  The First Encounter falls back to the
// closest player in that case (FixupCausedToPlayer), and its WatchPlayers
// passes on the player it saw.
//
// The fixes, as in The First Encounter:
//  - PrintCenterMessage: when the entity isn't a player, the message goes to
//    the closest live player to it (or to the first live player if there is
//    no entity).
//  - WatchPlayers' "close" event (0x60236EA0) passes on the closest live
//    player to what it watches (itself as a range watcher, otherwise its
//    Owner/Target), so the Trigger and whatever comes after it know who
//    caused it, for messages and for targets that act on that entity.  Its
//    "far" event stays without one, as in The First Encounter.  A range
//    watcher without an Owner/Target sends its event to everything in range
//    and is left alone.
// Off: [Gameplay] TriggerChainFix=0.  Installed in the game and in the World
// Editor (for Test Game) when the engine loads Entities.dll.
#include "alphafixes.h"

namespace {

// Entities.dll (13 Jan 2000, preferred base 0x601A0000)
constexpr uintptr_t ENT_BASE = 0x601A0000;
constexpr uintptr_t ENT_PRINTCENTERMESSAGE = 0x601A6620;   // void PrintCenterMessage(CEntity *, const CTString &, TIME, MessageSound)
constexpr uintptr_t ENT_PCM_WARNING_PUSH = 0x601A6649;     // push "Cannot print message (%s), triggering chain broken"
constexpr uintptr_t ENT_PCM_WARNING = 0x6027746C;
constexpr uintptr_t ENT_SENDTOTARGET = 0x601A1BE0;         // void SendToTarget(CEntity *, EventEType, CEntity *penCaused)
constexpr uintptr_t ENT_WATCH_SENDCLOSE = 0x60236EA0;      // CWatchPlayers: send the close event

constexpr size_t EN_FLAGS = 0x10;                          // CEntity::en_ulFlags, ENF_ALIVE = 8
constexpr size_t EN_POSITION = 0x1C;                       // CEntity::en_plPlacement.pl_PositionVector
constexpr size_t WP_OWNER = 0xD0;                          // CWatchPlayers::m_penOwner "Owner/Target"
constexpr size_t WP_RANGEWATCHER = 0xDC;                   // m_bRangeWatcher
constexpr size_t WP_EVENTCLOSE = 0xE0;                     // m_eetEventClose

using PrintCenterMessage_t = void(__cdecl *)(char *penCaused, const void *pstrMessage, DWORD tmLength, DWORD mssSound);
using SendToTarget_t = void(__cdecl *)(char *penTarget, int eetEvent, char *penCaused);
using WatchSend_t = void(__fastcall *)(char *pwp, void *pvEdx);
using GetMaxPlayers_t = long(__cdecl *)(void);
using GetPlayerEntity_t = char **(__cdecl *)(char **ppenResult, long iPlayer);   // returns a CEntityPointer
using RemReference_t = void(__fastcall *)(char *pen, void *pvEdx);
using IsOfClass_t = BOOL(__cdecl *)(char *pen, const char *strClass);

PrintCenterMessage_t _pfnPrintCenterMessage = nullptr;
SendToTarget_t _pfnSendToTarget = nullptr;
WatchSend_t _pfnWatchSendClose = nullptr;
GetMaxPlayers_t _pfnGetMaxPlayers = nullptr;
GetPlayerEntity_t _pfnGetPlayerEntity = nullptr;
RemReference_t _pfnRemReference = nullptr;
IsOfClass_t _pfnIsOfClass = nullptr;
bool _bInstalled = false;

// The closest live player to an entity; with no entity, the first live player.
char *ClosestPlayer(const char *penNear)
{
  char *penBest = nullptr;
  float fBest = 0.0f;
  long ctPlayers = _pfnGetMaxPlayers();
  for (long i = 0; i < ctPlayers; i++) {
    char *pen = nullptr;
    _pfnGetPlayerEntity(&pen, i);
    if (pen == nullptr) continue;
    _pfnRemReference(pen, nullptr);   // the CEntityPointer's reference; the game still holds the player
    if (!(*(ULONG *)(pen + EN_FLAGS) & 8)) continue;
    if (penNear == nullptr) return pen;
    const float *pfA = (const float *)(pen + EN_POSITION), *pfB = (const float *)(penNear + EN_POSITION);
    float fDX = pfA[0] - pfB[0], fDY = pfA[1] - pfB[1], fDZ = pfA[2] - pfB[2];
    float fDist = fDX * fDX + fDY * fDY + fDZ * fDZ;
    if (penBest == nullptr || fDist < fBest) {
      penBest = pen;
      fBest = fDist;
    }
  }
  return penBest;
}

void __cdecl PrintCenterMessage_Hook(char *penCaused, const void *pstrMessage, DWORD tmLength, DWORD mssSound)
{
  if (g_cfg.bTriggerChainFix && (penCaused == nullptr || !_pfnIsOfClass(penCaused, "Player"))) {
    char *penPlayer = ClosestPlayer(penCaused);
    if (penPlayer != nullptr) penCaused = penPlayer;
  }
  _pfnPrintCenterMessage(penCaused, pstrMessage, tmLength, mssSound);
}

void __fastcall WatchSendClose_Hook(char *pwp, void *pvEdx)
{
  char *penOwner = *(char **)(pwp + WP_OWNER);
  if (!g_cfg.bTriggerChainFix || (*(int *)(pwp + WP_RANGEWATCHER) && penOwner == nullptr)) {
    _pfnWatchSendClose(pwp, pvEdx);
    return;
  }
  // the same entity the alpha measures the players' distance from
  char *penCenter = *(int *)(pwp + WP_RANGEWATCHER) ? pwp : penOwner;
  _pfnSendToTarget(penOwner, *(int *)(pwp + WP_EVENTCLOSE), ClosestPlayer(penCenter));
}

}  // namespace

void InstallTriggerChainFix(HMODULE hEntities)
{
  if (_bInstalled || hEntities == nullptr) return;
  _bInstalled = true;
  auto Rebase = [hEntities](uintptr_t ul) { return ul - ENT_BASE + (uintptr_t)hEntities; };
  // push ebp; mov ebp, esp; push -1
  static const BYTE aubPrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };
  // push ebp; mov ebp, esp; sub esp, 0x18; mov eax, [ecx+0xDC]; test eax, eax; je
  static const BYTE aubWatch[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x8B, 0x81, 0xDC, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x74 };
  const BYTE *pubPush = (const BYTE *)Rebase(ENT_PCM_WARNING_PUSH);
  _pfnGetMaxPlayers = (GetMaxPlayers_t)EngineProc("?GetMaxPlayers@CEntity@@SAJXZ");
  _pfnGetPlayerEntity = (GetPlayerEntity_t)EngineProc("?GetPlayerEntity@CEntity@@SA?AVCEntityPointer@@J@Z");
  _pfnRemReference = (RemReference_t)EngineProc("?RemReference@CEntity@@QAEXXZ");
  _pfnIsOfClass = (IsOfClass_t)EngineProc("?IsOfClass@@YAHPAVCEntity@@PBD@Z");
  _pfnSendToTarget = (SendToTarget_t)Rebase(ENT_SENDTOTARGET);
  bool bEngine = _pfnGetMaxPlayers && _pfnGetPlayerEntity && _pfnRemReference && _pfnIsOfClass;
  bool bMessage = bEngine && CodeMatches(Rebase(ENT_PRINTCENTERMESSAGE), aubPrologue, sizeof(aubPrologue))
               && pubPush[0] == 0x68 && *(const uint32_t *)(pubPush + 1) == (uint32_t)Rebase(ENT_PCM_WARNING)
               && HookFunction((void *)Rebase(ENT_PRINTCENTERMESSAGE), (void *)PrintCenterMessage_Hook,
                               (void **)&_pfnPrintCenterMessage, "PrintCenterMessage");
  bool bWatch = bEngine && CodeMatches(Rebase(ENT_SENDTOTARGET), aubPrologue, sizeof(aubPrologue))
             && CodeMatches(Rebase(ENT_WATCH_SENDCLOSE), aubWatch, sizeof(aubWatch))
             && HookFunction((void *)Rebase(ENT_WATCH_SENDCLOSE), (void *)WatchSendClose_Hook,
                             (void **)&_pfnWatchSendClose, "CWatchPlayers close event");
  Log("Trigger messages without a player: %s", bMessage ? "installed" : "code differs, not installed");
  Log("WatchPlayers passes on the player: %s", bWatch ? "installed" : "code differs, not installed");
}
