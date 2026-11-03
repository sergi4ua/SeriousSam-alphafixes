// What the alpha's Entities.dll shares between its classes: entity info, the
// events every class uses, and TFE-style helpers rebuilt on the alpha's engine
// calls.  See Engine/Engine.h for how these headers were made.
#pragma once

#include <Engine/Engine.h>

#define ENTITIES_API __declspec(dllimport)

// Entities.dll of 13 Jan 2000; the code addresses in EnemyBase.cpp are only
// valid for this build.
#define ENTITIES_TIMESTAMP 0x387DDB83UL

// --- entity info (CEntity::GetEntityInfo) ------------------------------------

enum EntityInfoBodyType {
  EIBT_FLESH = 1,
  EIBT_WATER = 2,
  EIBT_ROCK = 3,
  EIBT_FIRE = 4,
  EIBT_AIR = 5,
  EIBT_BONES = 6,
  EIBT_WOOD = 7,
  EIBT_METAL = 8,
  EIBT_ROBOT = 9,
  EIBT_ICE = 10,
};

struct EntityInfo {
  enum EntityInfoBodyType Eeibt;
  FLOAT fMass;
  FLOAT vSourceCenter[3];   // relative to the entity, for shooting from it
  FLOAT vTargetCenter[3];   // relative to the entity, for shooting at it
};
static_assert(offsetof(EntityInfo, vTargetCenter) == 0x14, "EntityInfo layout");

// Absolute position of a point given relative to an entity (alpha Entities.dll).
void GetEntityInfoPosition(CEntity *pen, FLOAT *pf, FLOAT3D &vPos);

// --- events of Entities.dll ---------------------------------------------------

#define EVENTCODE_EEnd    0x00000006
#define EVENTCODE_EReturn 0x00000009
DECLARE_IMPORTED_EVENT(EEnd, ENTITIES_API)
DECLARE_IMPORTED_EVENT(EReturn, ENTITIES_API)

// sent by triggers, switches and the like to their targets
#define EVENTCODE_ETrigger 0x00000007
class ETrigger : public CEntityEvent {
public:
  CEntityPointer penCaused;        // 0x08, who caused the trigger
  ENTITIES_API ETrigger(void);
  ENTITIES_API virtual ~ETrigger();
  ENTITIES_API virtual CEntityEvent *MakeCopy(void);
};
static_assert(sizeof(ETrigger) == 0x0C, "ETrigger is 12 bytes in the alpha");

#define EVENTCODE_EStop       0x00000000
#define EVENTCODE_EStart      0x00000001
#define EVENTCODE_EActivate   0x00000002
#define EVENTCODE_EDeactivate 0x00000003
DECLARE_IMPORTED_EVENT(EStop, ENTITIES_API)
DECLARE_IMPORTED_EVENT(EActivate, ENTITIES_API)
DECLARE_IMPORTED_EVENT(EDeactivate, ENTITIES_API)
class EStart : public CEntityEvent {
public:
  CEntityPointer penCaused;        // 0x08
  ENTITIES_API EStart(void);
  ENTITIES_API virtual ~EStart();
  ENTITIES_API virtual CEntityEvent *MakeCopy(void);
};
static_assert(sizeof(EStart) == 0x0C, "EStart is 12 bytes in the alpha");

// points for the player (the alpha has no ESecretFound)
#define EVENTCODE_EReceiveScore 0x0000000C
class EReceiveScore : public CEntityEvent {
public:
  INDEX iPoints;                   // 0x08
  ENTITIES_API EReceiveScore(void);
  ENTITIES_API virtual ~EReceiveScore();
  ENTITIES_API virtual CEntityEvent *MakeCopy(void);
};
static_assert(sizeof(EReceiveScore) == 0x0C, "EReceiveScore is 12 bytes in the alpha");

// --- triggering (src/Common.cpp) -------------------------------------------------

// The alpha's event types for SendToTarget(), as in later versions up to
// EET_STOPATTACK.
enum EventEType {
  EET_START = 0,
  EET_STOP = 1,
  EET_TRIGGER = 2,
  EET_IGNORE = 3,
  EET_ACTIVATE = 4,
  EET_DEACTIVATE = 5,
  EET_ENVIRONMENTSTART = 6,
  EET_ENVIRONMENTSTOP = 7,
  EET_STARTATTACK = 8,
  EET_STOPATTACK = 9,
};
ENTITIES_API extern CEntityPropertyEnumType EventEType_enum;

enum MessageSound {
  MSS_NONE = 0,
  MSS_INFO = 1,
};
ENTITIES_API extern CEntityPropertyEnumType MessageSound_enum;

// The alpha's own functions (Entities.dll doesn't export them).
void SendToTarget(CEntity *penSendEvent, enum EventEType eetEventType, CEntity *penCaused = NULL);
void SendInRange(CEntity *penSource, enum EventEType eetEventType, const FLOATaabbox3D &boxRange);
// the alpha's: shows the message to penCaused, which must be a player
void PrintCenterMessage(CEntity *penCaused, const CTString &strMessage, TIME tmLength, enum MessageSound mssSound);
// The First Encounter's: with no player in the triggering chain, the closest
// live player to penThis gets the message.
CEntity *FixupCausedToPlayer(CEntity *penThis, CEntity *penCaused);
void PrintCenterMessage(CEntity *penThis, CEntity *penCaused, const CTString &strMessage, TIME tmLength,
  enum MessageSound mssSound);

// The alpha has no translation tables: strings are used as they are.
#define TRANS(str) str
inline const CTString &TranslateConst(const CTString &str) { return str; }

// --- model helpers ------------------------------------------------------------

// The later SDK sets up a model with these.  The alpha has no such helpers but
// does the same with CEntity calls, which these wrap.  The model object
// argument is kept for compatibility; it is always the entity's own model.
inline void SetComponents(CEntity *pen, CModelObject &mo, ULONG ulIDModel, ULONG ulIDTexture,
  ULONG ulIDReflectionTexture, ULONG ulIDSpecularTexture, ULONG ulIDBumpTexture)
{
  (void)mo; (void)ulIDBumpTexture;   // the alpha has no bump maps on models
  pen->SetModel(ulIDModel);
  pen->SetModelMainTexture(ulIDTexture);
  if (ulIDReflectionTexture > 0) pen->SetModelReflectionTexture(ulIDReflectionTexture);
  if (ulIDSpecularTexture > 0) pen->SetModelSpecularTexture(ulIDSpecularTexture);
}

inline void AddAttachmentToModel(CEntity *pen, CModelObject &mo, INDEX iAttachment, ULONG ulIDModel,
  ULONG ulIDTexture, ULONG ulIDReflectionTexture, ULONG ulIDSpecularTexture, ULONG ulIDBumpTexture)
{
  (void)mo; (void)ulIDBumpTexture;
  pen->AddAttachment(iAttachment, ulIDModel, ulIDTexture);
  if (ulIDReflectionTexture > 0) pen->SetModelAttachmentReflectionTexture(iAttachment, ulIDReflectionTexture);
  if (ulIDSpecularTexture > 0) pen->SetModelAttachmentSpecularTexture(iAttachment, ulIDSpecularTexture);
}
