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
