// The alpha's BasicEffect class (Entities.dll), as far as other classes use it.
#pragma once

#include <Entities/Common.h>

// Effect types.  The alpha's table has no names; the alpha's own Teleport
// spawns 16 (later SDKs call it BET_TELEPORT).  The alpha has 0-17.
enum BasicEffectType {
  BET_NONE = 0,
  BET_TELEPORT = 16,
};

extern "C" ENTITIES_API CDLLEntityClass CBasicEffect_DLLClass;

// Unlike later versions, the alpha's has no colour multiplier.
#define EVENTCODE_ESpawnEffect 0x02590000
class ESpawnEffect : public CEntityEvent {
public:
  enum BasicEffectType betType;    // 0x08
  FLOAT3D vNormal;                 // 0x0C
  FLOAT3D vStretch;                // 0x18
  ENTITIES_API ESpawnEffect(void);
  ENTITIES_API virtual ~ESpawnEffect();
  ENTITIES_API virtual CEntityEvent *MakeCopy(void);
};
static_assert(sizeof(ESpawnEffect) == 0x24, "ESpawnEffect is 36 bytes in the alpha");
