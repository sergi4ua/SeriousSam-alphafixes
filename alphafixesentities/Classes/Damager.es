229
%{
#include "StdH.h"
%}

// The First Encounter's Damager, ported to the alpha.  Changes: the alpha has
// no "Abyss" damage type, so the default type is Impact; its enum tables
// have no NameForValue(), so DamageTypeName() looks the name up; and a Start
// event works like a Trigger event, with the entity that caused it: the
// alpha's Touch Field sends Start events unless told otherwise (The First
// Encounter's sends Trigger events).

class CDamager: CRationalEntity {
name      "Damager";
thumbnail "Thumbnails\\Damager.tbn";
features  "HasName", "IsTargetable";

properties:
  1 CTString m_strName          "Name" 'N' = "Damager",
  2 CTString m_strDescription = "",
  3 enum DamageType m_dmtType "Type" 'Y' = DMT_IMPACT,    // type of damage
  4 FLOAT m_fAmmount "Ammount" 'A' = 1000.0f,             // ammount of damage
  5 CEntityPointer m_penToDamage "Entity to Damage" 'E',  // entity to damage, NULL to damage the triggerer
  6 BOOL m_bDamageFromTriggerer "DamageFromTriggerer" 'S' = FALSE,  // make the triggerer inflictor of the damage

components:
  1 model   MODEL_TELEPORT     "Models\\Editor\\Copier.mdl",
  2 texture TEXTURE_TELEPORT   "Models\\Editor\\Copier.tex",

functions:
  const CTString &GetDescription(void) const {
    return m_strDescription;
  }

  // the damage type's name in the engine's table
  const char *DamageTypeName(INDEX iValue) const {
    for (INDEX i = 0; i < DamageType_enum.epet_ctValues; i++) {
      if (DamageType_enum.epet_aepevValues[i].epev_iValue == iValue) {
        return DamageType_enum.epet_aepevValues[i].epev_strName;
      }
    }
    return "";
  }

  // damage the entity to damage, or with none, the entity that caused the event
  void DamageEntity(CEntity *penCaused) {
    CEntity *penInflictor = this;
    if (m_bDamageFromTriggerer) {
      penInflictor = penCaused;
    }

    if (m_penToDamage!=NULL) {
      InflictDirectDamage(m_penToDamage, penInflictor,
        m_dmtType, m_fAmmount,
        m_penToDamage->GetPlacement().pl_PositionVector, FLOAT3D(0,1,0));
    } else if (penCaused!=NULL) {
      InflictDirectDamage(penCaused, penInflictor,
        m_dmtType, m_fAmmount,
        penCaused->GetPlacement().pl_PositionVector, FLOAT3D(0,1,0));
    }
  }

procedures:
  Main()
  {
    InitAsEditorModel();
    SetPhysicsFlags(EPF_MODEL_IMMATERIAL);
    SetCollisionFlags(ECF_IMMATERIAL);

    // set appearance
    SetModel(MODEL_TELEPORT);
    SetModelMainTexture(TEXTURE_TELEPORT);

    ((CTString&)m_strDescription).PrintF("%s:%g",
      DamageTypeName(INDEX(m_dmtType)), m_fAmmount);

    while (TRUE) {
      // wait to someone trigger you and then damage it
      wait() {
        on (ETrigger eTrigger) : {
          DamageEntity(eTrigger.penCaused);
          stop;
        }
        on (EStart eStart) : {
          DamageEntity(eStart.penCaused);
          stop;
        }
        otherwise() : {
          resume;
        };
      };

      // wait a bit to recover
      autowait(0.1f);
    }
  }
};
