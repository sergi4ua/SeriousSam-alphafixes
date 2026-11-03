// Triggering helpers (SDK/Entities/Common.h).
//
// SendToTarget, SendInRange and PrintCenterMessage are the alpha's own
// functions, called at their address in Entities.dll (checked by
// InitEnemyBase() in EnemyBase.cpp).  FixupCausedToPlayer and the
// PrintCenterMessage that takes the sending entity are rebuilt from The First
// Encounter: the alpha's PrintCenterMessage shows nothing when no player
// started the triggering chain.
#include "StdH.h"

extern void *_pfnSendToTarget;
extern void *_pfnSendInRange;
extern void *_pfnPrintCenterMessage;

void SendToTarget(CEntity *penSendEvent, enum EventEType eetEventType, CEntity *penCaused)
{
  typedef void (__cdecl *SendToTarget_t)(CEntity *, enum EventEType, CEntity *);
  ((SendToTarget_t)_pfnSendToTarget)(penSendEvent, eetEventType, penCaused);
}

void SendInRange(CEntity *penSource, enum EventEType eetEventType, const FLOATaabbox3D &boxRange)
{
  typedef void (__cdecl *SendInRange_t)(CEntity *, enum EventEType, const FLOATaabbox3D &);
  ((SendInRange_t)_pfnSendInRange)(penSource, eetEventType, boxRange);
}

void PrintCenterMessage(CEntity *penCaused, const CTString &strMessage, TIME tmLength, enum MessageSound mssSound)
{
  typedef void (__cdecl *PrintCenterMessage_t)(CEntity *, const CTString &, TIME, enum MessageSound);
  ((PrintCenterMessage_t)_pfnPrintCenterMessage)(penCaused, strMessage, tmLength, mssSound);
}

// The player who caused the chain, or with none, the closest live player.
CEntity *FixupCausedToPlayer(CEntity *penThis, CEntity *penCaused)
{
  if (penCaused != NULL && IsOfClass(penCaused, "Player")) {
    return penCaused;
  }
  CEntity *penClosest = NULL;
  FLOAT fClosest = 0.0f;
  for (INDEX iPlayer = 0; iPlayer < CEntity::GetMaxPlayers(); iPlayer++) {
    CEntity *penPlayer = CEntity::GetPlayerEntity(iPlayer);
    if (penPlayer == NULL || !(penPlayer->GetFlags() & ENF_ALIVE)) continue;
    FLOAT fDistance = (penPlayer->GetPlacement().pl_PositionVector - penThis->GetPlacement().pl_PositionVector).Length();
    if (penClosest == NULL || fDistance < fClosest) {
      penClosest = penPlayer;
      fClosest = fDistance;
    }
  }
  return penClosest;
}

void PrintCenterMessage(CEntity *penThis, CEntity *penCaused, const CTString &strMessage, TIME tmLength,
  enum MessageSound mssSound)
{
  penCaused = FixupCausedToPlayer(penThis, penCaused);
  if (penCaused != NULL) {
    PrintCenterMessage(penCaused, strMessage, tmLength, mssSound);
  }
}
