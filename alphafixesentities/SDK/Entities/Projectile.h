// The alpha's Projectile class (Entities.dll), as far as other classes use it.
#pragma once

#include <Entities/Common.h>

// Projectile types.  The alpha's table has no names, so these are named after
// the later SDK where the alpha's own enemies confirm the number (Headman 11-13,
// Catman 10, Cyborg and Walker 44).  The alpha also has 1-3, 14-29, 40-43, 45
// and 50.
enum ProjectileType {
  PRT_ROCKET = 0,
  PRT_CATMAN_FIRE = 10,
  PRT_HEADMAN_FIRECRACKER = 11,
  PRT_HEADMAN_ROCKETMAN = 12,
  PRT_HEADMAN_BOMBERMAN = 13,
  PRT_CYBORG_LASER = 44,
};

extern "C" ENTITIES_API CDLLEntityClass CProjectile_DLLClass;

#define EVENTCODE_ELaunchProjectile 0x01F50000
class ELaunchProjectile : public CEntityEvent {
public:
  CEntityPointer penLauncher;      // 0x08
  enum ProjectileType prtType;     // 0x0C
  FLOAT fSpeed;                    // 0x10, for propelled projectiles
  ENTITIES_API ELaunchProjectile(void);
  ENTITIES_API virtual ~ELaunchProjectile();
  ENTITIES_API virtual CEntityEvent *MakeCopy(void);
};
static_assert(sizeof(ELaunchProjectile) == 0x14, "ELaunchProjectile is 20 bytes in the alpha");
