// The alpha's CEnemyBase (Entities.dll, 13 Jan 2000), for deriving new enemies.
//
// Entities.dll exports only CEnemyBase's class table and SetDefaultProperties.
// The functions declared here are reached in one of three ways:
//   - imported, where the alpha exports them;
//   - through the original CEnemyBase vtable, for its virtual functions;
//   - at their address in this exact Entities.dll build (EnemyBase.cpp).
// The layout below is the alpha's: property offsets come from CEnemyBase's
// property table.  Names follow the later SDK where the property ID and use
// match; the rest are named after their ID.
#pragma once

#include <Entities/Common.h>
#include <Entities/Projectile.h>

extern "C" ENTITIES_API CDLLEntityClass CEnemyBase_DLLClass;

// Each enemy's watcher entity, which looks out for players.
class CWatcher : public CRationalEntity {
public:
  // The alpha's enemies all pass Cos(50) (or their view half-angle), their
  // attack distance, 0.1, their ignore range, 1, 500, 5, 1500 and 10.  What
  // the last five control isn't known.
  void SetParameters(FLOAT fCosViewAngle, FLOAT fSightRange, FLOAT f3, FLOAT fIgnoreRange,
    FLOAT f5, FLOAT f6, FLOAT f7, FLOAT f8, FLOAT f9);
};

// CEnemyBase procedures that derived classes call or override (state IDs)
#define STATE_CEnemyBase_LockOnEnemy 0x01360055
#define STATE_CEnemyBase_Death       0x01360065
#define STATE_CEnemyBase_MainLoop    0x01360068
#define STATE_CEnemyBase_Fire        0x01360079
#define STATE_CEnemyBase_Hit         0x0136007A

class CEnemyBase : public CMovableModelEntity {
public:
  CEntityPointer m_penWatcher;            // 0x29C   1
  FLOAT3D m_vStartPosition;               // 0x2A0   2
  CEntityPointer m_penEnemy;              // 0x2AC   3  current enemy
  CEntityPointer m_penProp04;             // 0x2B0   4
  CTString m_strDescription;              // 0x2B4   5
  CTString m_strName;                     // 0x2B8   6  "Name"
  CSoundObject m_soSound;                 // 0x2BC   7  voice: idle, sight, wound, death
  FLOAT m_fWalkSpeed;                     // 0x338  10
  ANGLE m_aWalkRotateSpeed;               // 0x33C  11
  FLOAT m_fAttackRunSpeed;                // 0x340  12
  ANGLE m_aAttackRotateSpeed;             // 0x344  13
  FLOAT m_fCloseRunSpeed;                 // 0x348  14
  ANGLE m_aCloseRotateSpeed;              // 0x34C  15
  FLOAT m_fAttackDistance;                // 0x350  20
  FLOAT m_fCloseDistance;                 // 0x354  21
  FLOAT m_fAttackFireTime;                // 0x358  22
  FLOAT m_fCloseFireTime;                 // 0x35C  23
  FLOAT m_fStopDistance;                  // 0x360  24
  FLOAT m_fIgnoreRange;                   // 0x364  25
  FLOAT m_fLockOnEnemyTime;               // 0x368  26  time to aim before firing
  FLOAT m_fBlowUpAmount;                  // 0x36C  40  blows up when health <= -this
  INDEX m_fBodyParts;                     // 0x370  41
  FLOAT m_fDamageWounded;                 // 0x374  42
  FLOAT3D m_vDamage;                      // 0x378  43
  FLOAT m_tmLastDamage;                   // 0x384  44
  CEntityPointer m_penProp45;             // 0x388  45
  FLOAT m_fProp50;                        // 0x38C  50
  FLOAT m_fMoveTime;                      // 0x390 133
  BOOL m_bProp51;                         // 0x394  51
  FLOAT3D m_vDesiredPosition;             // 0x398  52
  FLOAT3D m_vProp53;                      // 0x3A4  53
  FLOAT m_fMoveFrequency;                 // 0x3B0  54
  FLOAT m_fMoveSpeed;                     // 0x3B4  55
  ANGLE m_aRotateSpeed;                   // 0x3B8  56
  FLOAT m_fLockStartTime;                 // 0x3BC  57
  BOOL m_bProp58;                         // 0x3C0  58
  FLOAT m_fProp59;                        // 0x3C4  59
  BOOL m_bFadeOut;                        // 0x3C8 130
  FLOAT m_fFadeStartTime;                 // 0x3CC 131
  FLOAT m_fFadeTime;                      // 0x3D0 132
  FLOAT m_fShootTime;                     // 0x3D4  60
  FLOAT m_fDamageConfused;                // 0x3D8  61
  INDEX m_iChargeHitAnimation;            // 0x3DC  62
  FLOAT m_fChargeHitDamage;               // 0x3E0  63
  FLOAT m_fChargeHitAngle;                // 0x3E4  64
  FLOAT m_fChargeHitSpeed;                // 0x3E8  65
  FLOAT3D m_vShootTarget;                 // 0x3EC  66  where projectiles are aimed
  BOOL m_bPatrol;                         // 0x3F8  81  "Patrol"
  RANGE m_fPatrolAreaInner;               // 0x3FC  82  "Patrol Area Inner"
  RANGE m_fPatrolAreaOuter;               // 0x400  83  "Patrol Area Outer"
  CEntityPointer m_penDeathTarget;        // 0x404  84  "Death target"
  INDEX m_eetDeathType;                   // 0x408  85  "Death event type"
  BOOL m_bTemplate;                       // 0x40C  86  "Template"
  BOOL m_bWaitStart;                      // 0x410  87  "Wait start"
  RANGE m_fAttackRadius;                  // 0x414  88  "Radius of attack"
  COLOR m_colColor;                       // 0x418  89  "Color"
  BOOL m_bDeaf;                           // 0x41C  90  "Deaf"
  BOOL m_bBlind;                          // 0x420  91  "Blind"
  BOOL m_bStretchRandom;                  // 0x424 100  "Stretch Random"
  FLOAT m_fStretchHeight;                 // 0x428 101  "Stretch Height (Y%)"
  FLOAT m_fStretchWidth;                  // 0x42C 102  "Stretch Width (X%)"
  FLOAT m_fStretchDepth;                  // 0x430 103  "Strecth Depth (Z%)"
  FLOAT3D m_vStretchFactor;               // 0x434 104  the stretch ApplyStretch() uses
  CEntityPointer m_penMarker;             // 0x440 120  "Marker"
  FLOAT m_fProp121;                       // 0x444 121
  FLOAT m_fProp122;                       // 0x448 122
  FLOAT m_fProp123;                       // 0x44C 123
  BOOL m_bProp124;                        // 0x450 124
  CEntityPointer m_penMainMusicHolder;    // 0x454 140
  FLOAT m_tmLastFussTime;                 // 0x458 141
  FLOAT m_iScore;                         // 0x45C 142  points for the kill
  FLOAT m_fMaxHealth;                     // 0x460 143
  BOOL m_bBoss;                           // 0x464 144
  CWatcher *m_pWatcher;                   // 0x468  not a property: m_penWatcher, set by SpawnWatcher()
  ULONG m_ulInternal46C;                  // 0x46C  not a property

  // CEnemyBase's overrides of engine virtual functions
  void Read_t(CTStream *istr);
  void Precache(void);
  ENTITIES_API void SetDefaultProperties(void);
  const CTString &GetName(void) const;
  const CTString &GetDescription(void) const;
  BOOL IsTargetable(void) const;
  BOOL MovesByTargetedRoute(CTString &strTargetProperty) const;
  BOOL DropsMarker(CTFileName &fnmMarkerClass, CTString &strTargetProperty) const;
  BOOL AdjustShadingParameters(FLOAT3D &vLightDirection, COLOR &colLight, COLOR &colAmbient);
  void ReceiveDamage(CEntity *penInflictor, enum DamageType dmtType,
    FLOAT fDamageAmmount, const FLOAT3D &vHitPoint, const FLOAT3D &vDirection);
  BOOL FillEntityStatistics(struct EntityStats *pes);
#ifdef AFE_TRACE
  // test builds: logs the events every enemy of this DLL receives
  BOOL HandleEvent(const CEntityEvent &ee);
#endif

  // CEnemyBase's own virtual functions, in vtable order (slots 45-62).  The
  // first five have no names in the alpha; derived classes don't override them.
  virtual void Virtual45_BlowUp(void);
  virtual void Virtual46(void);
  virtual void Virtual47_SpawnEffect(void);
  virtual void Virtual48_SoundEvent(INDEX i);
  virtual void Virtual49_SoundEvent(void);
  virtual void StandingAnim(void);
  virtual void WalkingAnim(void);
  virtual void RunningAnim(void);
  virtual void RotatingAnim(void);
  virtual void ChargeAnim(void);
  virtual INDEX AnimForDamage(FLOAT fDamage);
  virtual void BlowUpNotify(void);
  virtual INDEX AnimForDeath(void);
  virtual void DeathNotify(void);
  virtual void IdleSound(void);
  virtual void SightSound(void);
  virtual void WoundSound(void);
  virtual void DeathSound(void);

  // non-virtual helpers of the alpha's CEnemyBase

  // Every alpha enemy's Main calls these two; the later SDK's CEnemyBase does
  // the same by itself.  Without a watcher, MainLoop crashes.
  //   ApplyStretch(): applies the "Stretch Random/Height/Width/Depth"
  //     properties on top of the model's current stretch.
  //   SpawnWatcher(): creates m_penWatcher (and m_pWatcher).  The alpha's
  //     enemies call it only if they aren't a template (m_bTemplate), then
  //     call m_pWatcher->SetParameters().
  void ApplyStretch(void);
  void SpawnWatcher(void);
  void StopMoving(void);
  CEntityPointer ShootProjectile(enum ProjectileType pt, const FLOAT3D &vOffset, const ANGLE3D &aOffset);
  // aims at m_vShootTarget
  void PreparePropelledProjectile(CPlacement3D &plProjectile, const FLOAT3D &vOffset, const ANGLE3D &aOffset);

  // The later SDK's forms of these, rebuilt on the above.
  void PreparePropelledProjectile(CPlacement3D &plProjectile, const FLOAT3D &vTarget,
    const FLOAT3D &vOffset, const ANGLE3D &aOffset)
  {
    m_vShootTarget = vTarget;
    PreparePropelledProjectile(plProjectile, vOffset, aOffset);
  }
  FLOAT3D CalcDelta(CEntity *penEntity)
  {
    return penEntity->GetPlacement().pl_PositionVector - GetPlacement().pl_PositionVector;
  }
  FLOAT CalcDist(CEntity *penEntity) { return CalcDelta(penEntity).Length(); }
};
static_assert(offsetof(CEnemyBase, m_penWatcher) == 0x29C, "");
static_assert(offsetof(CEnemyBase, m_soSound) == 0x2BC, "");
static_assert(offsetof(CEnemyBase, m_fWalkSpeed) == 0x338, "");
static_assert(offsetof(CEnemyBase, m_fBlowUpAmount) == 0x36C, "");
static_assert(offsetof(CEnemyBase, m_vShootTarget) == 0x3EC, "");
static_assert(offsetof(CEnemyBase, m_bPatrol) == 0x3F8, "");
static_assert(offsetof(CEnemyBase, m_penMarker) == 0x440, "");
static_assert(offsetof(CEnemyBase, m_iScore) == 0x45C, "");
static_assert(offsetof(CEnemyBase, m_bBoss) == 0x464, "");
static_assert(sizeof(CEnemyBase) == 0x470, "CEnemyBase is 0x470 bytes in the alpha");
