// The parts of the alpha's Engine.dll (13 Jan 2000) that entity classes use.
//
// There is no SDK for the alpha, so this is rebuilt from the engine's exports
// and its machine code.  Class layouts are the alpha's own, not the later SDK's:
// only the members entity code touches are named, the rest is padding, and the
// static_asserts below pin every named offset.  Functions are imported from
// Engine.dll by their exact decorated names; a wrong signature fails to link.
//
// Virtual functions are declared in the alpha's vtable order.  That order is
// what makes a class here binary compatible with the engine, so don't reorder,
// add or remove any of them.
#pragma once

#include <windows.h>
#include <stddef.h>
#include <math.h>

#define ENGINE_API __declspec(dllimport)

typedef long SLONG;
typedef unsigned long ULONG;
typedef short SWORD;
typedef unsigned short UWORD;
typedef unsigned char UBYTE;
typedef long INDEX;
typedef float FLOAT;
typedef float ANGLE;
typedef float RANGE;
typedef float TIME;
typedef unsigned long COLOR;

#ifndef ASSERT
#define ASSERT(expr) ((void)0)
#endif
#define ASSERTMSG(expr, msg) ((void)0)
#define ARRAYCOUNT(array) (sizeof(array) / sizeof((array)[0]))

inline FLOAT AngleDeg(FLOAT fDegrees) { return fDegrees; }
ENGINE_API FLOAT Cos(ANGLE a);   // angles are in degrees
inline FLOAT Sqrt(FLOAT f) { return (FLOAT)sqrt(f < 0.0f ? 0.0f : f); }
template <class Type> inline Type Abs(Type x) { return x < 0 ? -x : x; }
template <class Type> inline Type Min(Type a, Type b) { return a < b ? a : b; }
template <class Type> inline Type Max(Type a, Type b) { return a > b ? a : b; }

// --- math ------------------------------------------------------------------

// The engine's vector template.  The name and parameters matter: they are part
// of the decorated names of engine functions (Vector<float,3>).
template <class Type, int iDimensions>
class Vector {
public:
  Type vector[iDimensions];

  Vector(void) {}
  Vector(Type x, Type y, Type z) { vector[0] = x; vector[1] = y; vector[2] = z; }
  Type &operator()(int i) { return vector[i - 1]; }
  const Type &operator()(int i) const { return vector[i - 1]; }

  Vector operator+(const Vector &v) const { Vector r; for (int i = 0; i < iDimensions; i++) r.vector[i] = vector[i] + v.vector[i]; return r; }
  Vector operator-(const Vector &v) const { Vector r; for (int i = 0; i < iDimensions; i++) r.vector[i] = vector[i] - v.vector[i]; return r; }
  Vector operator-(void) const { Vector r; for (int i = 0; i < iDimensions; i++) r.vector[i] = -vector[i]; return r; }
  Vector operator*(Type f) const { Vector r; for (int i = 0; i < iDimensions; i++) r.vector[i] = vector[i] * f; return r; }
  Vector operator/(Type f) const { return *this * (Type(1) / f); }
  Vector &operator+=(const Vector &v) { for (int i = 0; i < iDimensions; i++) vector[i] += v.vector[i]; return *this; }
  Vector &operator-=(const Vector &v) { for (int i = 0; i < iDimensions; i++) vector[i] -= v.vector[i]; return *this; }
  Vector &operator*=(Type f) { for (int i = 0; i < iDimensions; i++) vector[i] *= f; return *this; }
  // dot product
  Type operator%(const Vector &v) const { Type t = 0; for (int i = 0; i < iDimensions; i++) t += vector[i] * v.vector[i]; return t; }
  Type Length(void) const { return (Type)sqrt(*this % *this); }
};
typedef Vector<FLOAT, 3> FLOAT3D;
typedef Vector<ANGLE, 3> ANGLE3D;

class CPlacement3D {
public:
  FLOAT3D pl_PositionVector;
  ANGLE3D pl_OrientationAngle;
  ENGINE_API CPlacement3D(void);
  ENGINE_API CPlacement3D(const FLOAT3D &vPosition, const ANGLE3D &aOrientation);
  ENGINE_API void AbsoluteToRelative(const CPlacement3D &plSystem);
  ENGINE_API void RelativeToAbsolute(const CPlacement3D &plSystem);
};

// --- strings and memory ----------------------------------------------------

ENGINE_API void *AllocMemory(SLONG slSize);
ENGINE_API void FreeMemory(void *pvMemory);
ENGINE_API char *StringDuplicate(const char *strOriginal);

class CTString {
public:
  char *str_String;
  ENGINE_API CTString(void);
  ENGINE_API CTString(const char *strCharString);
  ENGINE_API CTString(const CTString &strOriginal);
  ENGINE_API ~CTString();
  ENGINE_API CTString &operator=(const char *strCharString);
  ENGINE_API CTString &operator=(const CTString &strOther);
  operator const char *() const { return str_String; }
};

class CTFileName : public CTString {};

// --- forward declarations of engine classes used only by reference ---------

class CTStream;
class CWorld;
class CDrawPort;
class CLightSource;
class CForceStrength;
class CFogParameters;
class CHazeParameters;
class CMirrorParameters;
class CAnimData;
class CFieldSettings;
class CEntityClass;
class CDLLEntityClass;
class CSoundData;
class CEntity;
struct EntityStats;

// --- models and sounds -----------------------------------------------------

class CModelObject {
public:
  ENGINE_API void StretchModel(const FLOAT3D &vStretchFactor);
};

class CSoundObject {
public:
  UBYTE so_aubData[0x7C];   // the alpha's CSoundObject is 124 bytes
  ENGINE_API CSoundObject(void);
  ENGINE_API ~CSoundObject();
  ENGINE_API void SetOwner(CEntity *penOwner);
  ENGINE_API void Stop(void);
  ENGINE_API void Set3DParameters(FLOAT fFalloff, FLOAT fHotSpot, FLOAT fMaxVolume, FLOAT fPitch);
  ENGINE_API BOOL IsPlaying(void);
};
static_assert(sizeof(CSoundObject) == 0x7C, "CSoundObject is 124 bytes in the alpha");

// sound flags (CSoundObject::Play)
#define SOF_NONE        (0L)
#define SOF_LOOP        (1L << 0)
#define SOF_3D          (1L << 1)
#define SOF_VOLUMETRIC  (1L << 2)
#define SOF_SURROUND    (1L << 3)
#define SOF_LOCAL       (1L << 4)
#define SOF_SMOOTHCHANGE (1L << 5)

// model animation flags (CAnimObject::PlayAnim)
#define AOF_LOOPING     (1UL << 0)
#define AOF_NORESTART   (1UL << 1)
#define AOF_PAUSED      (1UL << 2)
#define AOF_SMOOTHCHANGE (1UL << 3)

// --- events ----------------------------------------------------------------

class CEntityEvent {
public:
  SLONG ee_slEvent;   // event code; the vtable pointer comes before it
  ENGINE_API CEntityEvent(SLONG slEventCode);
  ENGINE_API virtual ~CEntityEvent();
  virtual CEntityEvent *MakeCopy(void);
};
static_assert(sizeof(CEntityEvent) == 8, "CEntityEvent is 8 bytes in the alpha");

// engine events (Engine.dll)
#define EVENTCODE_EInternal 0x00010000
#define EVENTCODE_EVoid     0x00010001
#define EVENTCODE_EBegin    0x00010002
#define EVENTCODE_ETimer    0x00010003

#define DECLARE_IMPORTED_EVENT(name, api)                    \
  class name : public CEntityEvent {                         \
  public:                                                    \
    api name(void);                                          \
    api name(const name &eOther);                            \
    api virtual ~name();                                     \
    api virtual CEntityEvent *MakeCopy(void);                \
  };
DECLARE_IMPORTED_EVENT(EInternal, ENGINE_API)
DECLARE_IMPORTED_EVENT(EVoid, ENGINE_API)
DECLARE_IMPORTED_EVENT(EBegin, ENGINE_API)
DECLARE_IMPORTED_EVENT(ETimer, ENGINE_API)

// --- entity pointers -------------------------------------------------------

class CEntityPointer {
public:
  CEntity *ep_pen;
  inline CEntityPointer(void) : ep_pen(nullptr) {}
  inline CEntityPointer(CEntity *pen);
  inline CEntityPointer(const CEntityPointer &penOther);
  inline ~CEntityPointer(void);
  inline CEntityPointer &operator=(CEntity *pen);
  inline CEntityPointer &operator=(const CEntityPointer &penOther);
  inline CEntity *operator->(void) const { return ep_pen; }
  inline operator CEntity *(void) const { return ep_pen; }
  inline CEntity &operator*(void) const { return *ep_pen; }
};
static_assert(sizeof(CEntityPointer) == 4, "CEntityPointer is a pointer");

// --- entity classes: properties, components, handlers ----------------------

class CEntityPropertyEnumValue {
public:
  INDEX epev_iValue;
  const char *epev_strName;
};

class CEntityPropertyEnumType {
public:
  CEntityPropertyEnumValue *epet_aepevValues;
  INDEX epet_ctValues;
};

#define EP_ENUMBEG(typename) extern CEntityPropertyEnumValue typename##_values[]; \
  CEntityPropertyEnumValue typename##_values[] = {
#define EP_ENUMVALUE(value, name) {value, name}
#define EP_ENUMEND(typename) }; CEntityPropertyEnumType typename##_enum = { \
  typename##_values, sizeof(typename##_values) / sizeof(CEntityPropertyEnumValue) }

class CEntityProperty {
public:
  enum PropertyType {
    EPT_PARENT = 0,
    EPT_ENUM = 1,
    EPT_BOOL = 2,
    EPT_FLOAT = 3,
    EPT_COLOR = 4,
    EPT_STRING = 5,
    EPT_RANGE = 6,
    EPT_ENTITYPTR = 7,
    EPT_FILENAME = 8,
    EPT_INDEX = 9,
    EPT_ANIMATION = 10,
    EPT_ILLUMINATIONTYPE = 11,
    EPT_FLOATAABBOX3D = 12,
    EPT_ANGLE = 13,
    EPT_FLOAT3D = 14,
    EPT_ANGLE3D = 15,
    EPT_FLOATplane3D = 16,
    EPT_MODELOBJECT = 17,
    EPT_PLACEMENT3D = 18,
    EPT_ANIMOBJECT = 19,
    EPT_FILENAMENODEP = 20,
    EPT_SOUNDOBJECT = 21,
  };
  enum PropertyType ep_eptType;
  CEntityPropertyEnumType *ep_pepetEnumType;
  ULONG ep_ulID;
  SLONG ep_slOffset;
  const char *ep_strName;
  COLOR ep_colColor;
  char ep_chShortcut;

  ENGINE_API CEntityProperty(enum PropertyType eptType, CEntityPropertyEnumType *pepetEnumType,
    ULONG ulID, SLONG slOffset, char *strName, char chShortcut);
};
static_assert(sizeof(CEntityProperty) == 28, "CEntityProperty is 28 bytes in the alpha");

enum EntityComponentType {
  ECT_TEXTURE = 1,
  ECT_MODEL = 2,
  ECT_CLASS = 3,
  ECT_SOUND = 4,
};

class CEntityComponent {
public:
  enum EntityComponentType ec_ectType;
  SLONG ec_slID;
  CTFileName ec_fnmComponent;
  void *ec_pvData;
  INDEX ec_ctUsed;
  // the file name starts with "EFNM", which the constructor skips
  ENGINE_API CEntityComponent(enum EntityComponentType ectNewType, ULONG ulNewID, char *strNewFilename);
  ENGINE_API ~CEntityComponent(void);
};
static_assert(sizeof(CEntityComponent) == 20, "CEntityComponent is 20 bytes in the alpha");

// --- entities --------------------------------------------------------------

// entity flags (en_ulFlags)
#define ENF_SELECTED        (1UL << 0)
#define ENF_ZONING          (1UL << 1)
#define ENF_DELETED         (1UL << 2)
#define ENF_ALIVE           (1UL << 3)

// physics and collision flags, as the alpha's own enemies use them
#define EPF_MODEL_WALKING   0x40000207UL
#define EPF_MODEL_CORPSE    0x20000207UL
#define ECF_MODEL           0x00017C02UL
#define ECF_CORPSE          0x00003420UL

enum DamageType {
  DMT_EXPLOSION = 1,
  DMT_PROJECTILE = 2,
  DMT_CLOSERANGE = 3,
  DMT_BULLET = 4,
  DMT_DROWNING = 5,
  DMT_IMPACT = 6,
  DMT_BRUSH = 7,
  DMT_BURNING = 8,
  DMT_ACID = 9,
  DMT_TELEPORT = 10,
  DMT_FREEZING = 11,
  DMT_CANNONBALL = 12,
  DMT_CANNONBALL_EXPLOSION = 13,
  DMT_DISCONNECTED = 14,
  DMT_NONE = 9999,
};

class CEntity {
public:
  // Members at their alpha offsets (the vtable pointer is at 0).
  UBYTE en_aubPad04[0x10 - 0x04];
  ULONG en_ulFlags;                       // 0x10
  UBYTE en_aubPad14[0x18 - 0x14];
  INDEX en_ctReferences;                  // 0x18
  CPlacement3D en_plPlacement;            // 0x1C
  UBYTE en_aubPad34[0x5C - 0x34];
  CModelObject *en_pmoModelObject;        // 0x5C
  UBYTE en_aubPad60[0xB0 - 0x60];
  FLOAT en_fHealth;                       // 0xB0 (CLiveEntity)
  UBYTE en_aubPadB4[0x120 - 0xB4];
  FLOAT en_fDensity;                      // 0x120
  UBYTE en_aubPad124[0x134 - 0x124];
  FLOAT3D en_vGravityDir;                 // 0x134 (CMovableEntity)
  FLOAT en_fGravityA;                     // 0x140 (CMovableEntity)
  UBYTE en_aubPad144[0x29C - 0x144];

  typedef BOOL (CEntity::*pEventHandler)(const CEntityEvent &e);

  ENGINE_API CEntity(void);

  // The 45 virtual functions of the alpha's CEntity, in vtable order.
  ENGINE_API virtual void Copy(CEntity &enOther, ULONG ulFlags);                                   //  0
  ENGINE_API virtual CEntity &operator=(CEntity &enOther);                                         //  1
  ENGINE_API virtual void Read_t(CTStream *istr);                                                  //  2
  ENGINE_API virtual void Write_t(CTStream *ostr);                                                 //  3
  ENGINE_API virtual void Precache(void);                                                          //  4
  ENGINE_API virtual void SetDefaultProperties(void);                                              //  5
  ENGINE_API virtual void OnInitialize(const CEntityEvent &eeInput);                               //  6
  ENGINE_API virtual void OnEnd(void);                                                             //  7
  ENGINE_API virtual const char *PrintStackDebug(void);                                            //  8
  ENGINE_API virtual ~CEntity();                                                                   //  9
  ENGINE_API virtual pEventHandler HandlerForStateAndEvent(SLONG slState, SLONG slEvent);          // 10
  ENGINE_API virtual BOOL HandleEvent(const CEntityEvent &ee);                                     // 11
  ENGINE_API virtual CPlacement3D GetLerpedPlacement(void) const;                                  // 12
  ENGINE_API virtual const CTString &GetName(void) const;                                          // 13
  ENGINE_API virtual const CTString &GetDescription(void) const;                                   // 14
  ENGINE_API virtual CEntityPointer GetTarget(void) const;                                         // 15
  ENGINE_API virtual BOOL IsTargetable(void) const;                                                // 16
  ENGINE_API virtual BOOL IsMarker(void) const;                                                    // 17
  ENGINE_API virtual BOOL MovesByTargetedRoute(CTString &strTargetProperty) const;                 // 18
  ENGINE_API virtual BOOL DropsMarker(CTFileName &fnmMarkerClass, CTString &strTargetProperty) const; // 19
  ENGINE_API virtual CLightSource *GetLightSource(void);                                           // 20
  ENGINE_API virtual const CTString &GetForceName(INDEX iForce);                                   // 21
  ENGINE_API virtual void GetForce(INDEX iForce, const FLOAT3D &vPoint,
    CForceStrength &fsGravity, CForceStrength &fsField);                                           // 22
  ENGINE_API virtual const CTString &GetFogName(INDEX iFog);                                       // 23
  ENGINE_API virtual BOOL GetFog(INDEX iFog, CFogParameters &fpFog);                               // 24
  ENGINE_API virtual const CTString &GetHazeName(INDEX iHaze);                                     // 25
  ENGINE_API virtual BOOL GetHaze(INDEX iHaze, CHazeParameters &hpHaze, FLOAT3D &vViewDir);        // 26
  ENGINE_API virtual const CTString &GetMirrorName(INDEX iMirror);                                 // 27
  ENGINE_API virtual BOOL GetMirror(INDEX iMirror, CMirrorParameters &mpMirror);                   // 28
  ENGINE_API virtual CAnimData *GetAnimData(SLONG slPropertyOffset);                               // 29
  ENGINE_API virtual BOOL AdjustShadingParameters(FLOAT3D &vLightDirection,
    COLOR &colLight, COLOR &colAmbient);                                                           // 30
  ENGINE_API virtual CFieldSettings *GetFieldSettings(void);                                       // 31
  ENGINE_API virtual void RenderParticles(void);                                                   // 32
  ENGINE_API virtual INDEX GetCollisionBoxIndex(void);                                             // 33
  ENGINE_API virtual void RenderGameView(CDrawPort *pdp, void *pvUserData);                        // 34
  ENGINE_API virtual void MirrorAndStretch(FLOAT fStretch, BOOL bMirrorX);                         // 35
  ENGINE_API virtual void ReceiveDamage(CEntity *penInflictor, enum DamageType dmtType,
    FLOAT fDamageAmmount, const FLOAT3D &vHitPoint, const FLOAT3D &vDirection);                    // 36
  ENGINE_API virtual BOOL ReceiveItem(const CEntityEvent &ee);                                     // 37
  ENGINE_API virtual void *GetEntityInfo(void);                                                    // 38
  ENGINE_API virtual BOOL FillEntityStatistics(struct EntityStats *pes);                           // 39
  ENGINE_API virtual void PreMoving(void);                                                         // 40
  ENGINE_API virtual void DoMoving(void);                                                          // 41
  ENGINE_API virtual void PostMoving(void);                                                        // 42
  ENGINE_API virtual void ChecksumForSync(ULONG &ulCRC);                                           // 43
  ENGINE_API virtual void DumpSync_t(CTStream &strm);                                              // 44

  // non-virtual
  ENGINE_API void AddReference(void);
  ENGINE_API void RemReference(void);
  ENGINE_API void Initialize(const CEntityEvent &eeInput);
  ENGINE_API void InitAsModel(void);
  ENGINE_API void SetPhysicsFlags(ULONG ulFlags);
  ENGINE_API void SetCollisionFlags(ULONG ulFlags);
  ENGINE_API void SetFlags(ULONG ulFlags);
  ULONG GetFlags(void) const { return en_ulFlags; }
  ENGINE_API const CPlacement3D &GetPlacement(void) const;
  ENGINE_API CModelObject *GetModelObject(void);
  ENGINE_API void ModelChangeNotify(void);
  ENGINE_API void SetModel(SLONG idModelComponent);
  ENGINE_API void SetModelMainTexture(SLONG idTextureComponent);
  ENGINE_API void SetModelReflectionTexture(SLONG idTextureComponent);
  ENGINE_API void SetModelSpecularTexture(SLONG idTextureComponent);
  ENGINE_API void AddAttachment(INDEX iAttachment, ULONG ulIDModel, ULONG ulIDTexture);
  ENGINE_API void SetModelAttachmentReflectionTexture(INDEX iAttachment, SLONG idTextureComponent);
  ENGINE_API void SetModelAttachmentSpecularTexture(INDEX iAttachment, SLONG idTextureComponent);
  ENGINE_API void StartModelAnim(INDEX iNewModelAnim, ULONG ulFlags);
  // windows.h renames PlaySound to PlaySoundA, and so did the engine's build
  ENGINE_API void PlaySoundA(CSoundObject &so, SLONG idSoundComponent, SLONG slPlayType);
  ENGINE_API void PrecacheModel(SLONG slID);
  ENGINE_API void PrecacheTexture(SLONG slID);
  ENGINE_API void PrecacheSound(SLONG slID);
  ENGINE_API void PrecacheClass(SLONG slID, INDEX iUser);
  ENGINE_API CEntityPointer CreateEntity(const CPlacement3D &plPlacement, SLONG idModelComponent);
  ENGINE_API FLOAT FRnd(void);
  ENGINE_API ULONG IRnd(void);
};
static_assert(sizeof(CEntity) == 0x29C, "an alpha entity's engine part ends at 0x29C");
static_assert(offsetof(CEntity, en_ulFlags) == 0x10, "");
static_assert(offsetof(CEntity, en_ctReferences) == 0x18, "");
static_assert(offsetof(CEntity, en_plPlacement) == 0x1C, "");
static_assert(offsetof(CEntity, en_pmoModelObject) == 0x5C, "");
static_assert(offsetof(CEntity, en_fHealth) == 0xB0, "");
static_assert(offsetof(CEntity, en_fDensity) == 0x120, "");
static_assert(offsetof(CEntity, en_vGravityDir) == 0x134, "");
static_assert(offsetof(CEntity, en_fGravityA) == 0x140, "");

// The engine's entity class hierarchy.  All engine members are in CEntity
// above; these classes only carry the overrides that the alpha's
// CMovableModelEntity vtable holds, so that a derived vtable matches it.
class CLiveEntity : public CEntity {
public:
  ENGINE_API void SetHealth(FLOAT fHealth);
  ENGINE_API virtual void ReceiveDamage(CEntity *penInflictor, enum DamageType dmtType,
    FLOAT fDamageAmmount, const FLOAT3D &vHitPoint, const FLOAT3D &vDirection);
};

class CRationalEntity : public CLiveEntity {
public:
  ENGINE_API virtual const char *PrintStackDebug(void);
  ENGINE_API virtual BOOL HandleEvent(const CEntityEvent &ee);
  ENGINE_API virtual void ChecksumForSync(ULONG &ulCRC);
  ENGINE_API virtual void DumpSync_t(CTStream &strm);

  ENGINE_API void Jump(SLONG slThisState, SLONG slTargetState, BOOL bOverride, const CEntityEvent &eeInput);
  ENGINE_API void Call(SLONG slThisState, SLONG slTargetState, BOOL bOverride, const CEntityEvent &eeInput);
  ENGINE_API void Return(SLONG slThisState, const CEntityEvent &eeReturn);
  ENGINE_API void SetTimerAfter(TIME tmDelta);
  ENGINE_API void SetTimerAt(TIME tmAbsolute);
  ENGINE_API void UnsetTimer(void);
};

class CMovableEntity : public CRationalEntity {
public:
  ENGINE_API CMovableEntity(void);
  ENGINE_API virtual ~CMovableEntity();
  ENGINE_API virtual void Copy(CEntity &enOther, ULONG ulFlags);
  ENGINE_API virtual void OnInitialize(const CEntityEvent &eeInput);
  ENGINE_API virtual void OnEnd(void);
  ENGINE_API virtual CPlacement3D GetLerpedPlacement(void) const;
  ENGINE_API virtual void PostMoving(void);

  ENGINE_API void SetDesiredTranslation(const FLOAT3D &vTranslation);
  ENGINE_API void SetDesiredRotation(const ANGLE3D &aRotation);
};

class CMovableModelEntity : public CMovableEntity {
public:
  ENGINE_API virtual void Read_t(CTStream *istr);
  ENGINE_API virtual void Write_t(CTStream *ostr);
  ENGINE_API virtual void SetDefaultProperties(void);
  ENGINE_API virtual INDEX GetCollisionBoxIndex(void);
  ENGINE_API virtual void PreMoving(void);
  ENGINE_API virtual void DoMoving(void);
};

ENGINE_API BOOL IsOfSameClass(CEntity *pen1, CEntity *pen2);

// --- CEntityPointer, after CEntity is known -----------------------------------

inline CEntityPointer::CEntityPointer(CEntity *pen) : ep_pen(pen) { if (ep_pen) ep_pen->AddReference(); }
inline CEntityPointer::CEntityPointer(const CEntityPointer &penOther) : ep_pen(penOther.ep_pen) { if (ep_pen) ep_pen->AddReference(); }
inline CEntityPointer::~CEntityPointer(void) { if (ep_pen) ep_pen->RemReference(); }
inline CEntityPointer &CEntityPointer::operator=(CEntity *pen)
{
  if (pen) pen->AddReference();
  if (ep_pen) ep_pen->RemReference();
  ep_pen = pen;
  return *this;
}
inline CEntityPointer &CEntityPointer::operator=(const CEntityPointer &penOther) { return *this = penOther.ep_pen; }

// --- entity class tables (CDLLEntityClass) -----------------------------------

struct CEventHandlerEntry {
  SLONG ehe_slState;
  SLONG ehe_slBaseState;
  CEntity::pEventHandler ehe_pEventHandler;
  const char *ehe_strName;
};
static_assert(sizeof(CEventHandlerEntry) == 16, "CEventHandlerEntry is 16 bytes in the alpha");
#define DEBUGSTRING(str) (str)

class CDLLEntityClass {
public:
  CEntityProperty *dec_aepProperties;
  INDEX dec_ctProperties;
  CEventHandlerEntry *dec_aeheHandlers;
  INDEX dec_ctHandlers;
  CEntityComponent *dec_aecComponents;
  INDEX dec_ctComponents;
  const char *dec_strName;
  const char *dec_strIconFileName;
  INDEX dec_iID;
  CDLLEntityClass *dec_pdecBase;
  CEntity *(*dec_New)(void);
  void (*dec_OnInitClass)(void);
  void (*dec_OnEndClass)(void);
  void (*dec_OnPrecache)(CDLLEntityClass *pdec, INDEX iUser);
  void (*dec_OnWorldInit)(CWorld *pwoWorld);
  void (*dec_OnWorldTick)(CWorld *pwoWorld);
  void (*dec_OnWorldRender)(CWorld *pwoWorld);
  void (*dec_OnWorldEnd)(CWorld *pwoWorld);
};
static_assert(sizeof(CDLLEntityClass) == 18 * 4, "CDLLEntityClass is 18 fields in the alpha");

#define ENTITY_CLASSDEFINITION(classname, basename, descriptivename, iconfilename, id) \
  CDLLEntityClass classname##_DLLClass = {                                              \
    classname##_properties, classname##_propertiesct,                                    \
    classname##_handlers, classname##_handlersct,                                        \
    classname##_components, classname##_componentsct,                                    \
    descriptivename, iconfilename, id, &basename##_DLLClass,                             \
    &classname##_New, &classname##_OnInitClass, &classname##_OnEndClass,                 \
    &classname##_OnPrecache, &classname##_OnWorldInit, &classname##_OnWorldTick,         \
    &classname##_OnWorldRender, &classname##_OnWorldEnd }

// The ECC also writes this after every class; the alpha needs nothing from it.
#define DECLARE_CTFILENAME(name, string) static const char *name = string
