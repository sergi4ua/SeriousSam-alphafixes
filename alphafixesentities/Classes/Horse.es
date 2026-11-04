335
%{
#include "StdH.h"
#include "Models/Enemies/Catman/Catman.h"
#include "Models/Enemies/Catman/Horse.h"
%}

uses "Entities/EnemyBase";
uses "Entities/Catman";


%{
// info structure
static EntityInfo eiHorse = {
  EIBT_FLESH, 50000.0f,
  0.0f, 2.0f, 0.0f,
  0.0f, 1.5f, 0.0f,
};

#define RIDER_FRONT     FLOAT3D( 0.0f, 0.0f, 0.0f)
%}


class CHorse : CEnemyBase {
name      "Horse";
thumbnail "Thumbnails\\Horse.tbn";

properties:
 1 BOOL m_bFrontRider "Rider Front" 'H' = FALSE,     // front rider
  2 FLOAT m_fTurnEndTime "Turn End Time" 'T' = 0.0f, 
 	     3 CSoundObject m_soFeet,            // for running sound
  4 BOOL m_bRunSoundPlaying = FALSE,
  
components:
  0 class   CLASS_BASE        "Classes\\EnemyBase.ecl",
  1 model   MODEL_HORSE     "Models\\Enemies\\Catman\\Horse.mdl",
  2 texture   TEXTURE_HORSE     "Models\\Enemies\\Catman\\HORSE01.tex",
  3 model   MODEL_CATMAN      "Models\\Enemies\\Catman\\Catman.mdl",
  4 texture TEXTURE_SOLDIER   "Models\\Enemies\\Catman\\Catman11.tex",
  5 class   CLASS_CATMAN       "Classes\\Catman.ecl",

// ************** SOUNDS **************
 50 sound   SOUND_IDLE      "Models\\Enemies\\Catman\\Sounds\\HorseIdle.wav",
 51 sound   SOUND_SIGHT     "Models\\Enemies\\Catman\\Sounds\\Sight.wav",
 52 sound   SOUND_WOUND     "Models\\Enemies\\Catman\\Sounds\\Wound.wav",
 53 sound   SOUND_FIRE      "Models\\Enemies\\Catman\\Sounds\\Fire.wav",
 54 sound   SOUND_KICK      "Models\\Enemies\\Catman\\Sounds\\Kick.wav",
 55 sound   SOUND_DEATH     "Models\\Enemies\\Catman\\Sounds\\HorseDeath.wav",
 56 sound   SOUND_RUN       "Models\\Enemies\\Catman\\Sounds\\HorseRun.wav",
 57 sound   SOUND_KICK_HORSE "Models\\Enemies\\Catman\\Sounds\\HorseKick.wav",

functions:
  /* Entity info */
  void *GetEntityInfo(void) {
    return &eiHorse;
  };

    void Precache(void) {
    CEnemyBase::Precache();
    PrecacheModel(MODEL_CATMAN);
    PrecacheTexture(TEXTURE_SOLDIER);
    PrecacheModel(MODEL_HORSE);
    PrecacheTexture(TEXTURE_HORSE);
    PrecacheSound(SOUND_IDLE);
    PrecacheSound(SOUND_SIGHT);
    PrecacheSound(SOUND_WOUND);
    PrecacheSound(SOUND_FIRE);
    PrecacheSound(SOUND_KICK);
    PrecacheSound(SOUND_KICK_HORSE);
    PrecacheSound(SOUND_DEATH);
    PrecacheSound(SOUND_RUN);
    PrecacheClass(CLASS_CATMAN, CMT_ROGUE);
  };

  // the rider's model while it sits on the horse, NULL after it has dropped
  CModelObject *Rider(void) {
    CAttachmentModelObject *pamo = GetModelObject()->GetAttachmentModel(HORSE_ATTACHMENT_ATTACHMENT_RIDER);
    return pamo != NULL ? &pamo->amo_moModelObject : NULL;
  };
  void RiderAnim(INDEX iAnim, ULONG ulFlags) {
    CModelObject *pmo = Rider();
    if (pmo != NULL) {
      pmo->PlayAnim(iAnim, ulFlags);
    }
  };

  // Turns towards m_vDesiredPosition at m_aRotateSpeed and moves at
  // m_fMoveSpeed, as the later SDK's CEnemyBase::SetDesiredMovement() (the
  // alpha's has no such function to call).
  void SetDesiredMovement(void) {
    if (m_aRotateSpeed > 0.0f) {
      FLOAT3D vDir = m_vDesiredPosition - GetPlacement().pl_PositionVector;
      const FLOATmatrix3D &m = GetRotationMatrix();
      FLOAT fFront = -(vDir(1)*m(1,3) + vDir(2)*m(2,3) + vDir(3)*m(3,3));
      FLOAT fLeft = -(vDir(1)*m(1,1) + vDir(2)*m(2,1) + vDir(3)*m(3,1));
      ANGLE aWanted = (ANGLE)(atan2(fLeft, fFront)*180.0/3.14159265358979);
      ANGLE aHeading;
      if (aWanted < -m_aRotateSpeed*m_fMoveFrequency) {
        aHeading = -m_aRotateSpeed;
      } else if (aWanted > m_aRotateSpeed*m_fMoveFrequency) {
        aHeading = m_aRotateSpeed;
      } else {
        aHeading = aWanted/m_fMoveFrequency;
      }
      SetDesiredRotation(ANGLE3D(aHeading, 0, 0));
    } else {
      SetDesiredRotation(ANGLE3D(0, 0, 0));
    }
    SetDesiredTranslation(FLOAT3D(0.0f, 0.0f, -m_fMoveSpeed));
  };

  /* Receive damage */
  void ReceiveDamage(CEntity *penInflictor, enum DamageType dmtType,
    FLOAT fDamageAmmount, const FLOAT3D &vHitPoint, const FLOAT3D &vDirection) 
  {
    // catman can't harm catman
    if (!IsOfClass(penInflictor, "Horse")) {
      CEnemyBase::ReceiveDamage(penInflictor, dmtType, fDamageAmmount, vHitPoint, vDirection);
    }
  };


  // damage anim
  INDEX AnimForDamage(FLOAT fDamage) {
    StartModelAnim(HORSE_ANIM_DEFAULT_ANIMATION, 0);
    RiderAnim(CATMAN_ANIM_WOUND01, AOF_NORESTART);
    return HORSE_ANIM_DEFAULT_ANIMATION;
  };

  // death
  INDEX AnimForDeath(void) {
    INDEX iAnim;
    iAnim = HORSE_ANIM_DEATH;
		    DeactivateRunningSound();
    StartModelAnim(iAnim, 0);
    return iAnim;
  };

  void DeathNotify(void) {
    // the horse model has one collision box
    ChangeCollisionBoxIndexWhenPossible(HORSE_COLLISION_BOX_PART_NAME);
    DropRiders(TRUE);
  };


  // drop riders
  void CreateRider(const FLOAT3D &vPos, INDEX iRider) {
    // mamutman start position
    CPlacement3D pl;
    pl.pl_OrientationAngle = ANGLE3D(0,0,0);
    pl.pl_PositionVector = vPos;
    pl.RelativeToAbsolute(GetPlacement());
    // create rider
    CEntityPointer pen = CreateEntity(pl, CLASS_CATMAN);
    ((CCatman&)*pen).m_bSpawned = TRUE;
    ((CCatman&)*pen).m_bSpawnedPosition = iRider;
    ((CCatman&)*pen).m_penEnemy = m_penEnemy;
    ((CCatman&)*pen).m_cmtType = CMT_ROGUE;
    pen->Initialize(EVoid());
  };

  void DropRiders(BOOL bAlways) {
    if (Rider() != NULL) {
      CreateRider(RIDER_FRONT, 0);
      RemoveAttachmentFromModel(*GetModelObject(), HORSE_ATTACHMENT_ATTACHMENT_RIDER);
    }
  };


  // virtual anim functions
  void StandingAnim(void) {
    StartModelAnim(HORSE_ANIM_DEFAULT_ANIMATION, AOF_LOOPING|AOF_NORESTART);
		    DeactivateRunningSound();

    RiderAnim(CATMAN_ANIM_STAND, AOF_LOOPING|AOF_NORESTART);
  };
  void WalkingAnim(void) {
    StartModelAnim(HORSE_ANIM_WALK, AOF_LOOPING|AOF_NORESTART);
		    ActivateRunningSound();

    RiderAnim(CATMAN_ANIM_STAND, AOF_LOOPING|AOF_NORESTART);
  };
  void RunningAnim(void) {
    StartModelAnim(HORSE_ANIM_RUN, AOF_LOOPING|AOF_NORESTART);
		    ActivateRunningSound();

    RiderAnim(CATMAN_ANIM_STAND, AOF_LOOPING|AOF_NORESTART);
  };
  void RotatingAnim(void) {
    StartModelAnim(HORSE_ANIM_WALK, AOF_LOOPING|AOF_NORESTART);
		    ActivateRunningSound();

    RiderAnim(CATMAN_ANIM_STAND, AOF_LOOPING|AOF_NORESTART);
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


 void ActivateRunningSound(void)
  {
    if (!m_bRunSoundPlaying) {
      PlaySound(m_soFeet, SOUND_RUN, SOF_3D|SOF_LOOP);
      m_bRunSoundPlaying = TRUE;
    }
  }
  void DeactivateRunningSound(void)
  {
    m_soFeet.Stop();
    m_bRunSoundPlaying = FALSE;
  };


procedures:
/************************************************************
 *                A T T A C K   E N E M Y                   *
 ************************************************************/
  Fire(EVoid) : CEnemyBase::Fire {
    // wait for a while
    StandingAnim();
    autowait(0.2f + FRnd()/4);

    // fire projectile
    StartModelAnim(HORSE_ANIM_DEFAULT_ANIMATION, 0);
		    DeactivateRunningSound();

    RiderAnim(CATMAN_ANIM_ATTACK02, AOF_NORESTART);

    autowait(0.15f);
    ShootProjectile(PRT_CATMAN_FIRE, FLOAT3D(0.0f, 1.5f, 0.5f), ANGLE3D(-8, 0, 0));
	   PlaySound(m_soSound, SOUND_FIRE, SOF_3D);

    autowait(0.15f);
    ShootProjectile(PRT_CATMAN_FIRE, FLOAT3D(0.0f, 1.5f, 0.5f), ANGLE3D(0.0f, 0, 0));
	   PlaySound(m_soSound, SOUND_FIRE, SOF_3D);

    autowait(0.05f);
    ShootProjectile(PRT_CATMAN_FIRE, FLOAT3D(0.0f, 1.5f, 0.5f), ANGLE3D(8.0f, 0, 0));
	   PlaySound(m_soSound, SOUND_FIRE, SOF_3D);

    autowait(0.15f);
    ShootProjectile(PRT_CATMAN_FIRE, FLOAT3D(0.0f, 1.5f, 0.5f), ANGLE3D(0.0f, 0, 0));
	   PlaySound(m_soSound, SOUND_FIRE, SOF_3D);

    autowait(0.05f);
    ShootProjectile(PRT_CATMAN_FIRE, FLOAT3D(0.0f, 1.5f, 0.5f), ANGLE3D(8.0f, 0, 0));
	   PlaySound(m_soSound, SOUND_FIRE, SOF_3D);


    return EReturn();
  };


 // hit enemy
  Hit(EVoid) : CEnemyBase::Hit {
    m_vDesiredPosition = GetPlacement().pl_PositionVector*2.0f - m_penEnemy->GetPlacement().pl_PositionVector;
    m_fMoveSpeed = 0.0f;
    m_aRotateSpeed = 4000.0f; 
    m_fTurnEndTime = _pTimer->CurrentTick() + 0.25f;
    while (m_fTurnEndTime > _pTimer->CurrentTick()) {
      m_fMoveFrequency = 0.1f;
      wait(m_fMoveFrequency) {
        on (EBegin) : {
          SetDesiredMovement();
          resume;
        }
        on (ETimer) : { stop; }
      }
    }
    m_aRotateSpeed = 0.0f;

    StartModelAnim(HORSE_ANIM_KICK, 0);
    autowait(0.5f);
    PlaySound(m_soSound, SOUND_KICK_HORSE, SOF_3D);
    if (CalcDist(m_penEnemy) < m_fCloseDistance) {
      FLOAT3D vDirection = m_penEnemy->GetPlacement().pl_PositionVector-GetPlacement().pl_PositionVector;
      vDirection.Normalize();
      InflictDirectDamage(m_penEnemy, this, DMT_CLOSERANGE, 25.0f, FLOAT3D(0, 0, 0), vDirection);

      FLOAT3D vSpeed;
      GetHeadingDirection(0.0f, vSpeed);
      vSpeed = vSpeed * (-20.0f);
      KickEntity(m_penEnemy, vSpeed);
    }
    autowait(0.3f);

    m_vDesiredPosition = m_penEnemy->GetPlacement().pl_PositionVector;
    m_fMoveSpeed = 0.0f;
    m_aRotateSpeed = 2000.0f;
    m_fTurnEndTime = _pTimer->CurrentTick() + 0.25f;
    while (m_fTurnEndTime > _pTimer->CurrentTick()) {
      m_fMoveFrequency = 0.1f;
      wait(m_fMoveFrequency) {
        on (EBegin) : {
          SetDesiredMovement();
          resume;
        }
        on (ETimer) : { stop; }
      }
    }
    m_aRotateSpeed = 0.0f;

    return EReturn();
  };



/************************************************************
 *                       M  A  I  N                         *
 ************************************************************/
  Main(EVoid) {
    // declare yourself as a model
    InitAsModel();
    SetPhysicsFlags(EPF_MODEL_WALKING|EPF_HASLUNGS);
    SetCollisionFlags(ECF_MODEL);
    SetFlags(GetFlags()|ENF_ALIVE);
    SetHealth(35.0f);
    m_fMaxHealth = 35.0f;
    en_tmMaxHoldBreath = 5.0f;
    en_fDensity = 2000.0f;

    // set your appearance
    SetModel(MODEL_HORSE);
    SetModelMainTexture(TEXTURE_HORSE);;
	AddAttachmentToModel(this, *GetModelObject(), HORSE_ATTACHMENT_ATTACHMENT_RIDER,
      MODEL_CATMAN, TEXTURE_SOLDIER, TEXTURE_SOLDIER, TEXTURE_SOLDIER, 0);
    StandingAnim();
    // setup moving speed
    m_fWalkSpeed = FRnd() + 1.5f;
    m_aWalkRotateSpeed = AngleDeg(FRnd()*10.0f + 25.0f);
    m_fAttackRunSpeed = FRnd()*4.0f + 8.0f;
    m_aAttackRotateSpeed = AngleDeg(FRnd()*50 + 245.0f);
    m_fCloseRunSpeed = FRnd()*4.0f + 8.0f;
    m_aCloseRotateSpeed = AngleDeg(FRnd()*50 + 245.0f);
    // setup attack distances
    m_fAttackDistance = 40.0f;
    m_fCloseDistance = 3.5f; // kick reach, tune to taste
    m_fStopDistance = 10.0f;
    m_fAttackFireTime = 3.0f;
    m_fCloseFireTime = 1.0f;
    m_fIgnoreRange = 200.0f;
    // damage/explode properties
    m_fBlowUpAmount = 70.0f;
    m_fBodyParts = 4;
    m_fDamageWounded = 25.0f;
	m_iScore = 500;
			  m_soFeet.Set3DParameters(60.0f, 5.0f, 1.0f, 1.0f);
	    m_bRunSoundPlaying = FALSE;
    // the editor's Stretch Random/Height/Width/Depth, as the alpha's enemies do
    ApplyStretch();

    // As the alpha's enemies do: wait a moment, so that the rest happens in
    // the game and not when the editor places the horse.  Then a template (for
    // enemy spawners) stops, and every other horse spawns its own watcher,
    // with the alpha Catman's parameters.
    autowait(FRnd()*0.5f + 0.1f);
    if (m_bTemplate) {
      return;
    }
    SpawnWatcher();
    m_pWatcher->SetParameters(Cos(50.0f), 50.0f, 0.1f, 200.0f,
      1.0f, 500.0f, 5.0f, 1500.0f, 10.0f);

    // continue behavior in base class
    jump CEnemyBase::MainLoop();
  };
};
