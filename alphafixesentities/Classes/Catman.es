301
%{
#include "StdH.h"
#include "Models/Enemies/Catman/Catman.h"
%}

uses "Entities/EnemyBase";

enum CatmanType {
  0 CMT_SOLDIER    "Soldier",
  1 CMT_GENERAL    "General",
  2 CMT_ROGUE      "Rogue" 
};


%{
// info structure
static EntityInfo eiCatman = {
  EIBT_FLESH, 50000.0f,
  0.0f, 2.0f, 0.0f,
  0.0f, 1.5f, 0.0f,
};
#define CLOSE_HIT 2.0f
%}


class CCatman : CEnemyBase {
name      "Catman";
thumbnail "Thumbnails\\Catman.tbn";

properties:
  1 enum CatmanType m_cmtType "Type" 'T' = CMT_SOLDIER,
  2 BOOL m_bSpawned = FALSE,
  3 INDEX m_bSpawnedPosition = 0,
  
components:
  0 class   CLASS_BASE        "Classes\\EnemyBase.ecl",
  1 model   MODEL_CATMAN      "Models\\Enemies\\Catman\\Catman.mdl",
  2 texture TEXTURE_SOLDIER   "Models\\Enemies\\Catman\\Catman03.tex",
  3 texture TEXTURE_GENERAL  "Models\\Enemies\\Catman\\Catman07.tex",
  4 texture TEXTURE_ROGUE   "Models\\Enemies\\Catman\\Catman11.tex",
 5 model   MODEL_KNIFEITEM             "Models\\Weapons\\Knife\\Blade.mdl",
 6 texture TEXTURE_KNIFEITEM           "Models\\Weapons\\Knife\\KnifeItem.tex",
   7 model   MODEL_SWORD      "Models\\Enemies\\Catman\\Sword.mdl",
  82 texture TEXTURE_SWORD   "Models\\Enemies\\Catman\\Sword.tex",

// ************** SOUNDS **************
 50 sound   SOUND_IDLE      "Models\\Enemies\\Catman\\Sounds\\Idle.wav",
 51 sound   SOUND_SIGHT     "Models\\Enemies\\Catman\\Sounds\\Sight.wav",
 52 sound   SOUND_WOUND     "Models\\Enemies\\Catman\\Sounds\\Wound.wav",
 53 sound   SOUND_FIRE      "Models\\Enemies\\Catman\\Sounds\\Fire.wav",
 54 sound   SOUND_KICK      "Models\\Enemies\\Catman\\Sounds\\Kick.wav",
 55 sound   SOUND_DEATH     "Models\\Enemies\\Catman\\Sounds\\Death.wav",

functions:

  /* Entity info */
  void *GetEntityInfo(void) {
    return &eiCatman;
  };


    void Precache(void) {
    CEnemyBase::Precache();
    PrecacheModel(MODEL_CATMAN);
    PrecacheTexture(TEXTURE_SOLDIER);
    PrecacheSound(SOUND_IDLE);
    PrecacheSound(SOUND_SIGHT);
    PrecacheSound(SOUND_WOUND);
    PrecacheSound(SOUND_FIRE);
    PrecacheSound(SOUND_KICK);
    PrecacheSound(SOUND_DEATH);
  };

  /* Receive damage */
  void ReceiveDamage(CEntity *penInflictor, enum DamageType dmtType,
    FLOAT fDamageAmmount, const FLOAT3D &vHitPoint, const FLOAT3D &vDirection) 
  {
    // catman can't harm catman
    if (!IsOfClass(penInflictor, "Catman")) {
      CEnemyBase::ReceiveDamage(penInflictor, dmtType, fDamageAmmount, vHitPoint, vDirection);
    }
  };


  // damage anim
  INDEX AnimForDamage(FLOAT fDamage) {
    INDEX iAnim;
    switch (IRnd()%5) {
      case 0: iAnim = CATMAN_ANIM_WOUND01; break;
      case 1: iAnim = CATMAN_ANIM_WOUND02; break;
      case 2: iAnim = CATMAN_ANIM_WOUND03; break;
	  case 3: iAnim = CATMAN_ANIM_FALL01; break;
      case 4: iAnim = CATMAN_ANIM_FALL02; break;
      default: ASSERTALWAYS("Catman unknown damage");
    }
    StartModelAnim(iAnim, 0);
    return iAnim;
  };

  // death
  INDEX AnimForDeath(void) {
    INDEX iAnim;
    switch (IRnd()%2) {
      case 0: iAnim = CATMAN_ANIM_DEATH01; break;
      case 1: iAnim = CATMAN_ANIM_DEATH02; break;
      default: ASSERTALWAYS("Catman unknown death");
    }
    StartModelAnim(iAnim, 0);
    return iAnim;
  };

  void DeathNotify(void) {
    ChangeCollisionBoxIndexWhenPossible(CATMAN_COLLISION_BOX_DEATH);
    en_fDensity = 500.0f;
  };

  // virtual anim functions
  void StandingAnim(void) {
    StartModelAnim(CATMAN_ANIM_STAND, AOF_LOOPING|AOF_NORESTART);
  };
  void WalkingAnim(void) {
    StartModelAnim(CATMAN_ANIM_WALK, AOF_LOOPING|AOF_NORESTART);
  };
  void RunningAnim(void) {
    StartModelAnim(CATMAN_ANIM_RUN, AOF_LOOPING|AOF_NORESTART);
  };
  void RotatingAnim(void) {
    StartModelAnim(CATMAN_ANIM_WALK, AOF_LOOPING|AOF_NORESTART);
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
  SoldierFire(EVoid) {
    // wait for a while
    StandingAnim();
    autowait(0.2f + FRnd()/4);

    // fire projectile
    StartModelAnim(CATMAN_ANIM_ATTACK02, 0);
    ShootProjectile(PRT_CATMAN_FIRE, FLOAT3D(0.0f, 1.5f, 0.5f), ANGLE3D(0, 0, 0));
    PlaySound(m_soSound, SOUND_FIRE, SOF_3D);
    autowait(FRnd()/3+0.6f);

    return EReturn();
  };


  GeneralFire(EVoid) {
    // wait for a while
    StandingAnim();
    autowait(0.2f + FRnd()/4);

    // fire projectile
    StartModelAnim(CATMAN_ANIM_ATTACK02, 0);
    ShootProjectile(PRT_CATMAN_FIRE, FLOAT3D(0.0f, 1.5f, 0.5f), ANGLE3D(2, 0, 0));
    PlaySound(m_soSound, SOUND_FIRE, SOF_3D);
    ShootProjectile(PRT_CATMAN_FIRE, FLOAT3D(0.0f, 1.5f, 0.5f), ANGLE3D(-2, 0, 0));
    PlaySound(m_soSound, SOUND_FIRE, SOF_3D);
    autowait(FRnd()/3+0.6f);

    return EReturn();
  };

  RogueFire(EVoid) {
    // wait for a while
    StandingAnim();
    autowait(0.2f + FRnd()/4);

    // fire projectile
    StartModelAnim(CATMAN_ANIM_ATTACK02, 0);
    ShootProjectile(PRT_CATMAN_FIRE, FLOAT3D(0.0f, 1.5f, 0.5f), ANGLE3D(2, 0, 0));
    PlaySound(m_soSound, SOUND_FIRE, SOF_3D);
    ShootProjectile(PRT_CATMAN_FIRE, FLOAT3D(0.0f, 1.5f, 0.5f), ANGLE3D(0, 0, 0));
    PlaySound(m_soSound, SOUND_FIRE, SOF_3D);
    ShootProjectile(PRT_CATMAN_FIRE, FLOAT3D(0.0f, 1.5f, 0.5f), ANGLE3D(-2, 0, 0));
    PlaySound(m_soSound, SOUND_FIRE, SOF_3D);
    autowait(FRnd()/3+0.6f);

    return EReturn();
  };


/************************************************************
 *                A T T A C K   E N E M Y                   *
 ************************************************************/


  Fire(EVoid) : CEnemyBase::Fire {
    // fire projectile
    switch (m_cmtType) {
      case CMT_SOLDIER: jump SoldierFire(); break;
      case CMT_GENERAL: jump GeneralFire(); break;
      case CMT_ROGUE: jump RogueFire(); break;
    }
    return EReturn();
  };


    // hit enemy
  Hit(EVoid) : CEnemyBase::Hit {
    // close attack
    StartModelAnim(CATMAN_ANIM_ATTACK01, 0);
    autowait(0.5f);
    PlaySound(m_soSound, SOUND_KICK, SOF_3D);
    if (CalcDist(m_penEnemy) < m_fCloseDistance) {
      FLOAT3D vDirection = m_penEnemy->GetPlacement().pl_PositionVector-GetPlacement().pl_PositionVector;
      vDirection.Normalize();
      if (m_cmtType == CMT_ROGUE) {
        InflictDirectDamage(m_penEnemy, this, DMT_CLOSERANGE, 25.0f, FLOAT3D(0, 0, 0), vDirection);
      } else if (m_cmtType == CMT_GENERAL) {
        InflictDirectDamage(m_penEnemy, this, DMT_CLOSERANGE, 10.0f, FLOAT3D(0, 0, 0), vDirection);
      } else {
        InflictDirectDamage(m_penEnemy, this, DMT_CLOSERANGE, 3.5f, FLOAT3D(0, 0, 0), vDirection);
      }
    }
    autowait(0.3f);
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
    en_tmMaxHoldBreath = 5.0f;
    en_fDensity = 2000.0f;

  // set your appearance
    SetModel(MODEL_CATMAN);
   StandingAnim();
    switch (m_cmtType) {
      case CMT_SOLDIER:
        // set your texture
        SetModelMainTexture(TEXTURE_SOLDIER);
        SetHealth(15.0f);
        m_fMaxHealth = 15.0f;
        // damage/explode properties
        // setup attack distances
     m_fAttackDistance = 40.0f;
     m_fCloseDistance = 3.5f;
     m_fStopDistance = 10.0f;
     m_fAttackFireTime = 3.0f;
     m_fCloseFireTime = 1.0f;
	    m_fBlowUpAmount = 35.0f;
    m_fIgnoreRange = 200.0f;
        m_iScore = 100;
        break;

      case CMT_GENERAL:
        // set your texture
        SetModelMainTexture(TEXTURE_GENERAL);
        AddAttachment(CATMAN_ATTACHMENT_KNIFE, MODEL_KNIFEITEM, TEXTURE_KNIFEITEM);
        SetHealth(30.0f);
        m_fMaxHealth = 30.0f;
        // damage/explode properties
        // setup attack distances
     m_fAttackDistance = 60.0f;
     m_fCloseDistance = 3.5f;
     m_fStopDistance = 10.0f;
     m_fAttackFireTime = 3.0f;
     m_fCloseFireTime = 1.0f;
	    m_fBlowUpAmount = 60.0f;
    m_fIgnoreRange = 200.0f;
        m_iScore = 200;
        break;

      case CMT_ROGUE:
        // set your texture
        SetModelMainTexture(TEXTURE_ROGUE);
        AddAttachment(CATMAN_ATTACHMENT_SWORD, MODEL_SWORD, TEXTURE_SWORD);
        SetHealth(60.0f);
        m_fMaxHealth = 60.0f;
        // damage/explode properties
        // setup attack distances
     m_fAttackDistance = 75.0f;
     m_fCloseDistance = 3.5f;
     m_fStopDistance = 10.0f;
     m_fAttackFireTime = 3.0f;
     m_fCloseFireTime = 1.0f;
	    m_fBlowUpAmount = 100.0f;
    m_fIgnoreRange = 200.0f;
        m_iScore = 500;
        break;
    }


    m_fWalkSpeed = FRnd() + 1.5f;
    m_aWalkRotateSpeed = AngleDeg(FRnd()*10.0f + 25.0f);
    m_fAttackRunSpeed = FRnd()*2.0f + 4.0f;
    m_aAttackRotateSpeed = AngleDeg(FRnd()*50 + 245.0f);
    m_fCloseRunSpeed = FRnd()*2.0f + 4.0f;
    m_aCloseRotateSpeed = AngleDeg(FRnd()*50 + 245.0f);
    // damage/explode properties
    m_fBodyParts = 4;
    m_fDamageWounded = 0.0f;
    // the editor's Stretch Random/Height/Width/Depth, as the alpha's enemies do
    ApplyStretch();

    // As the alpha's own Catman does: wait a moment, so that the rest happens
    // in the game and not when the editor places the catman.  Then a template
    // (for enemy spawners) stops, and every other catman spawns its own
    // watcher, with the alpha Catman's parameters.
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
