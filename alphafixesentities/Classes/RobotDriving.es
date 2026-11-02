333
%{
#include "StdH.h"
#include "Models/Enemies/Robots/DrivingWheel/Robot.h"
#include "Models/Enemies/Robots/DrivingSpider/DrivingSpider.h"
%}

uses "Entities/EnemyBase";
uses "Entities/Projectile";

enum RobotDrivingChar {
  0 RDC_R2D2      "R2D2",
  1 RDC_SPIDER    "Spider",
};

%{
// info structure
static EntityInfo eiRobotDriving = {
  EIBT_ROBOT, 50000.0f, // mass[kg]
  0.0f, 1.5f, 0.0f,   // source
  0.0f, 1.5f, 0.0f,   // target
};

#define FIRE_POS  FLOAT3D(0.0f, 1.0f, -1.0f)
#define R2D2_LEFT  FLOAT3D(0.8f, 1.0f, -1.0f)
#define R2D2_RIGHT  FLOAT3D(-0.8f, 1.0f, -1.0f)
%}


class CRobotDriving : CEnemyBase {
name      "RobotDriving";
thumbnail "Thumbnails\\RobotDriving.tbn";

properties:
  1 enum RobotDrivingChar m_rdcChar   "Character" 'C' = RDC_R2D2,
  2 FLOAT m_fSize = 1.0f,

  10 CSoundObject m_soFire0,
  11 CSoundObject m_soFire1,

  12 CSoundObject m_soFeet,              
  13 BOOL m_bRunSoundPlaying = FALSE,
  
components:
  0 class   CLASS_BASE          "Classes\\EnemyBase.ecl",
  1 class   CLASS_PROJECTILE    "Classes\\Projectile.ecl",

 10 model   MODEL_R2D2          "Models\\Enemies\\Robots\\DrivingWheel\\Robot.mdl",
 11 texture TEXTURE_R2D2        "Models\\Enemies\\Robots\\DrivingWheel\\Robot.tex",

 12 model   MODEL_SPIDER        "Models\\Enemies\\Robots\\DrivingSpider\\DrivingSpider.mdl",
 13 texture TEXTURE_SPIDER      "Models\\Enemies\\Robots\\DrivingSpider\\DrivingSpider.tex",

  14 model   MODEL_WEAPON        "Models\\Enemies\\Robots\\DrivingSpider\\Weapon.mdl",
 15 texture TEXTURE_WEAPON      "Models\\Enemies\\Robots\\DrivingSpider\\Weapon.tex",

   // ************** REFLECTIONS **************
202 texture TEX_REFL_LIGHTMETAL01       "Models\\ReflectionTextures\\LightMetal01.tex",

// ************** SPECULAR **************
211 texture TEX_SPEC_MEDIUM             "Models\\SpecularTextures\\Medium.tex",
212 texture TEX_SPEC_STRONG             "Models\\SpecularTextures\\Strong.tex",

// ************** SOUNDS **************
 50 sound   SOUND_IDLE        "Models\\Enemies\\Robots\\Sounds\\Idle.wav",
 51 sound   SOUND_SIGHT       "Models\\Enemies\\Robots\\Sounds\\Sight.wav",
 52 sound   SOUND_WOUND       "Models\\Enemies\\Robots\\Sounds\\Wound.wav",
 53 sound   SOUND_FIRE_LASER  "Models\\Enemies\\Robots\\Sounds\\FireLaser.wav",
 55 sound   SOUND_FIRE_BOMB        "Models\\Enemies\\Robots\\Sounds\\FireBomb.wav",
 56 sound   SOUND_DEATH       "Models\\Enemies\\Robots\\Sounds\\Death.wav",
  57 sound   SOUND_WALK       "Models\\Enemies\\Robots\\Sounds\\Walk.wav",
   58 sound   SOUND_DRIVING       "Models\\Enemies\\Robots\\Sounds\\Driving.wav",

functions:
  /* Entity info */
  void *GetEntityInfo(void) {
    return &eiRobotDriving;
  };

      void Precache(void) {
    CEnemyBase::Precache();
    PrecacheModel(MODEL_R2D2);
    PrecacheTexture(TEXTURE_R2D2);
	PrecacheModel(MODEL_SPIDER);
    PrecacheTexture(TEXTURE_SPIDER);
    PrecacheModel(MODEL_WEAPON);
    PrecacheTexture(TEXTURE_WEAPON);
    PrecacheSound(SOUND_IDLE);
    PrecacheSound(SOUND_SIGHT);
    PrecacheSound(SOUND_WOUND);
    PrecacheSound(SOUND_FIRE_LASER);
    PrecacheSound(SOUND_FIRE_BOMB);
    PrecacheSound(SOUND_DEATH);
    PrecacheSound(SOUND_WALK);
    PrecacheSound(SOUND_DRIVING);
	PrecacheClass(CLASS_PROJECTILE, PRT_CYBORG_LASER);
	PrecacheClass(CLASS_PROJECTILE, PRT_HEADMAN_BOMBERMAN);
  };

  /* Receive damage */
  void ReceiveDamage(CEntity *penInflictor, enum DamageType dmtType,
    FLOAT fDamageAmmount, const FLOAT3D &vHitPoint, const FLOAT3D &vDirection) 
  {
    // can't harm each other
    if (!IsOfSameClass(penInflictor, this)) {
      CEnemyBase::ReceiveDamage(penInflictor, dmtType, fDamageAmmount, vHitPoint, vDirection);
    }
  };

  // virtual anim functions
  void StandingAnim(void) {
    DeactivateRunningSound();
    if (m_rdcChar == RDC_R2D2) {
      StartModelAnim(ROBOT_ANIM_DEFAULT_ANIMATION, AOF_LOOPING|AOF_NORESTART);
    } else {
      StartModelAnim(DRIVINGSPIDER_ANIM_DEFAULT_ANIMATION, AOF_LOOPING|AOF_NORESTART);
    }
  };
  void WalkingAnim(void) {
    ActivateRunningSound();
    if (m_rdcChar == RDC_R2D2) {
      // R2D2 rides on wheels, no separate walk anim - keep default pose while driving
      StartModelAnim(ROBOT_ANIM_DEFAULT_ANIMATION, AOF_LOOPING|AOF_NORESTART);
    } else {
      StartModelAnim(DRIVINGSPIDER_ANIM_WALK, AOF_LOOPING|AOF_NORESTART);
    }
  };
  void RunningAnim(void) {
    ActivateRunningSound();
    if (m_rdcChar == RDC_R2D2) {
      StartModelAnim(ROBOT_ANIM_DEFAULT_ANIMATION, AOF_LOOPING|AOF_NORESTART);
    } else {
      StartModelAnim(DRIVINGSPIDER_ANIM_WALK, AOF_LOOPING|AOF_NORESTART);
    }
  };
  void RotatingAnim(void) {
    ActivateRunningSound();
    if (m_rdcChar == RDC_R2D2) {
      StartModelAnim(ROBOT_ANIM_DEFAULT_ANIMATION, AOF_LOOPING|AOF_NORESTART);
    } else {
      StartModelAnim(DRIVINGSPIDER_ANIM_WALK, AOF_LOOPING|AOF_NORESTART);
    }
  };

  // looped movement sound (R2D2 - driving, Spider - walk)
  void ActivateRunningSound(void) {
    if (!m_bRunSoundPlaying) {
      if (m_rdcChar == RDC_R2D2) {
        PlaySound(m_soFeet, SOUND_DRIVING, SOF_3D|SOF_LOOP);
      } else {
        PlaySound(m_soFeet, SOUND_WALK, SOF_3D|SOF_LOOP);
      }
      m_bRunSoundPlaying = TRUE;
    }
  };
  void DeactivateRunningSound(void) {
    m_soFeet.Stop();
    m_bRunSoundPlaying = FALSE;
  };

  // virtual sound functions
  void IdleSound(void) {
    PlaySound(m_soSound, SOUND_IDLE, SOF_3D);
  };
  void SightSound(void) {
    PlaySound(m_soSound, SOUND_SIGHT, SOF_3D);
  };
  void WoundSound(void) {
    PlaySound(m_soSound, SOUND_WOUND, SOF_3D);
  };
  void DeathSound(void) {
    PlaySound(m_soSound, SOUND_DEATH, SOF_3D);
  };

procedures:
/************************************************************
 *                A T T A C K   E N E M Y                   *
 ************************************************************/
  Fire(EVoid) : CEnemyBase::Fire {
    // r2d2
    if (m_rdcChar==RDC_R2D2) {
      // to fire
      m_fLockOnEnemyTime = 0.5f;//GetModelObject()->GetAnimLenght(WALKER_ANIM_TOFIRE);
      autocall CEnemyBase::LockOnEnemy() EReturn;

      StopMoving();
      DeactivateRunningSound();

      StartModelAnim(ROBOT_ANIM_FIRE1, 0);
      ShootProjectile(PRT_CYBORG_LASER, R2D2_LEFT*m_fSize, ANGLE3D(0, 0, 0));
      PlaySound(m_soFire0, SOUND_FIRE_LASER, SOF_3D);
      autowait(0.25f);
      StartModelAnim(ROBOT_ANIM_FIRE2, 0);
      ShootProjectile(PRT_CYBORG_LASER, R2D2_RIGHT*m_fSize, ANGLE3D(0, 0, 0));
      PlaySound(m_soFire1, SOUND_FIRE_LASER, SOF_3D);

      // wait for a while
      StandingAnim();
      autowait(FRnd()*0.1f+0.1f);

    // spider
    } else if (TRUE) {

      // don't shoot if enemy above or below you more than 5 meters
      if (Abs(en_vGravityDir%CalcDelta(m_penEnemy)) > 5.0f) {
        return EEnd();
      }

      m_fLockOnEnemyTime = 0.5f;//GetModelObject()->GetAnimLenght(WALKER_ANIM_TOFIRE);
      autocall CEnemyBase::LockOnEnemy() EReturn;

      // hit bomb
      
      FLOAT fSpeed = Sqrt(en_fGravityA*(CalcDist(m_penEnemy)*1.25f));
      // target enemy body
      EntityInfo *peiTarget = (EntityInfo*) (m_penEnemy->GetEntityInfo());
      FLOAT3D vShootTarget;
      GetEntityInfoPosition(m_penEnemy, peiTarget->vTargetCenter, vShootTarget);
      // launch
      CPlacement3D pl;
      PreparePropelledProjectile(pl, vShootTarget, FLOAT3D(0.0f, 1.5f, -0.7f), ANGLE3D(0, 45.0f, 0));
      CEntityPointer penProjectile = CreateEntity(pl, CLASS_PROJECTILE);
      ELaunchProjectile eLaunch;
      eLaunch.penLauncher = this;
      eLaunch.prtType = PRT_HEADMAN_BOMBERMAN;
      eLaunch.fSpeed = fSpeed;
      penProjectile->Initialize(eLaunch);

      //StartModelAnim(WALKER_ANIM_ATTACK02LEFT, AOF_LOOPING);
      StartModelAnim(DRIVINGSPIDER_ANIM_FIRE, 0);
      PlaySound(m_soFire0, SOUND_FIRE_BOMB, SOF_3D);
      StopMoving();
      DeactivateRunningSound();
      // wait for a while
      StandingAnim();
      autowait(FRnd()*0.1f+0.1f);
    }

    return EReturn();
  };



/************************************************************
 *                    D  E  A  T  H                         *
 ************************************************************/
  Death(EVoid) : CEnemyBase::Death {
    // stop moving
    StopMoving();
    DeactivateRunningSound();
    DeathSound();     // death sound

    // set physic flags
    SetPhysicsFlags(EPF_MODEL_CORPSE);
    SetCollisionFlags(ECF_CORPSE);
    // robots don't have different collision boxes, so no need to change
    if (m_rdcChar == RDC_R2D2) {
      StartModelAnim(ROBOT_ANIM_DEATH, 0);
    } else {
      StartModelAnim(DRIVINGSPIDER_ANIM_DEATH, 0);
    }
    return EEnd();
  };



/************************************************************
 *                       M  A  I  N                         *
 ************************************************************/
  Main(EVoid) {
    // declare yourself as a model
    InitAsModel();
    SetPhysicsFlags(EPF_MODEL_WALKING);
    SetCollisionFlags(ECF_MODEL);
    SetFlags(GetFlags()|ENF_ALIVE);

    SetHealth(40.0f);
    m_fMaxHealth = 40.0f;
    // The alpha has no robot debris (no m_bRobotBlowup): its blow-up always
    // throws flesh.  So robots never blow up and play their death animation.
    m_fBlowUpAmount = 100000.0f;
    m_fBodyParts = 6;
    m_fDamageWounded = 100000.0f;

    en_fDensity = 10000.0f;

    switch (m_rdcChar) {
    case RDC_R2D2: {
      // set your appearance
      m_fSize = 1.0f;
      SetComponents(this, *GetModelObject(), 
        MODEL_R2D2, TEXTURE_R2D2, TEX_REFL_LIGHTMETAL01,TEX_SPEC_STRONG,0);
      GetModelObject()->StretchModel(FLOAT3D(1,1,1));
      ModelChangeNotify();

      // setup moving speed
      m_fWalkSpeed = FRnd() + 1.5f;
      m_aWalkRotateSpeed = AngleDeg(FRnd()*10.0f + 500.0f);
      m_fAttackRunSpeed = FRnd() + 5.0f;
      m_aAttackRotateSpeed = AngleDeg(FRnd()*50 + 245.0f);
      m_fCloseRunSpeed = FRnd() + 5.0f;
      m_aCloseRotateSpeed = AngleDeg(FRnd()*50 + 245.0f);
      // setup attack distances
      m_fAttackDistance = 50.0f;
      m_fCloseDistance = 0.0f;
      m_fStopDistance = 8.0f;
      m_fAttackFireTime = 2.0f;
      m_fCloseFireTime = 1.0f;
      m_fIgnoreRange = 200.0f;

      m_iScore = 100;
                   } break;
    case RDC_SPIDER: {
      // set your appearance
      m_fSize = 1.0f;
      SetComponents(this, *GetModelObject(), 
        MODEL_SPIDER, TEXTURE_SPIDER, TEX_REFL_LIGHTMETAL01,TEX_SPEC_STRONG,0);
      AddAttachmentToModel(this, *GetModelObject(), DRIVINGSPIDER_ATTACHMENT_WEAPON,
        MODEL_WEAPON, TEXTURE_WEAPON, TEX_REFL_LIGHTMETAL01, TEX_SPEC_STRONG, 0);
      GetModelObject()->StretchModel(FLOAT3D(1.75,1.75,1.75));
      ModelChangeNotify();

      // setup moving speed
      m_fWalkSpeed = FRnd() + 1.5f;
      m_aWalkRotateSpeed = AngleDeg(FRnd()*10.0f + 500.0f);
      m_fAttackRunSpeed = FRnd() + 4.0f;
      m_aAttackRotateSpeed = AngleDeg(FRnd()*50 + 245.0f);
      m_fCloseRunSpeed = FRnd() + 4.0f;
      m_aCloseRotateSpeed = AngleDeg(FRnd()*50 + 245.0f);
      // setup attack distances
      m_fAttackDistance = 45.0f;
      m_fCloseDistance = 0.0f;
      m_fStopDistance = 20.0f;
      m_fAttackFireTime = 2.0f;
      m_fCloseFireTime = 1.5f;
      m_fIgnoreRange = 150.0f;

      m_iScore = 500;
          } break;
    default: ASSERT(FALSE);
    }
    // the editor's Stretch Random/Height/Width/Depth, as the alpha's enemies do
    ApplyStretch();
    StandingAnim();
    // set sound default parameters
    m_soFire0.Set3DParameters(160.0f, 5.0f, 1.0f, 1.0f);
    m_soFire1.Set3DParameters(160.0f, 5.0f, 1.0f, 1.0f);
    m_soFeet.Set3DParameters(15.0f, 5.0f, 1.0f, 1.0f);
    m_bRunSoundPlaying = FALSE;

    // As the alpha's own enemies do: wait a moment, so that the rest happens in
    // the game and not when the editor places the robot.  Then a template (for
    // enemy spawners) stops, and every other enemy spawns its own watcher.
    // Later versions do both in CEnemyBase::MainLoop.  The watcher parameters
    // are the alpha's usual ones, with this robot's attack distance and ignore
    // range.
    autowait(FRnd()*0.5f + 0.1f);
    if (m_bTemplate) {
      return;
    }
    SpawnWatcher();
    m_pWatcher->SetParameters(Cos(50.0f), m_fAttackDistance, 0.1f, m_fIgnoreRange,
      1.0f, 500.0f, 5.0f, 1500.0f, 10.0f);

    // continue behavior in base class
    jump CEnemyBase::MainLoop();
  };
};
