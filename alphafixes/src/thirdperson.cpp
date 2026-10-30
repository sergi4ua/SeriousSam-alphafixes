// Third-person camera like The First Encounter's.
//
// The alpha's third-person camera (CPlayerView::PostMoving, Entities.dll
// 0x602147D0, view type 3) takes the player's view angles pitched 30 degrees
// further down and backs off 4.5 m from the player's centre along them, with
// one ray against brushes only.  It never turns towards what the weapon aims
// at, so the aimed-at point shows well above the middle of the screen and the
// game's crosshair has to move about to show it.
//
// The First Encounter's camera (CPlayerView::SetCameraPosition, Croteam's
// Serious Engine source, Entities/PlayerView.es):
//  - starts from the view angles pitched 10 degrees down, without banking,
//  - backs off up to 5.75 m from the player's centre, testing five rays (the
//    middle one and 1 m above, below, left and right of it) against brushes
//    and model collision boxes, staying 0.5 m in front of what they hit and
//    a bit more in front of walls hit at an angle,
//  - then turns to look at the point the targeting ray hits: in third person
//    that ray goes straight ahead from the player's eyes (PlayerWeapons.es,
//    UpdateTargetingInfo), or to a point 50 m ahead if it hits nothing.
// So what the weapon aims at is always in the middle of the screen, and the
// crosshair stays there (crosshair.cpp).  Aiming and shooting are unchanged.
// One change: when the obstacle is gone, both games put the camera straight
// back at full distance, a jump of up to 5.75 m in one tick (backing along a
// wall past its end).  Here it eases back out; it still comes in at once, so it
// never goes into a wall.
// When walls or the floor have pushed the camera within 2 m of the player, the
// player is the viewer, which the renderer leaves out, instead of the camera
// (Player.es, GetViewEntity): the view isn't filled with the player's back.
#include "alphafixes.h"

#include <cmath>
#include <cstring>

namespace {

// Entities.dll (13 Jan 2000 build, preferred base)
constexpr uintptr_t ENT_VIEW_POSTMOVING = 0x602147D0;      // CPlayerView::PostMoving
constexpr uintptr_t ENT_GET_VIEW_ENTITY = 0x6020DCE0;      // CEntityPointer CPlayer::GetViewEntity(void)
constexpr uintptr_t ENT_ENTITY_INFO_POSITION = 0x601A1A60; // GetEntityInfoPosition(CEntity *, FLOAT *pfOffset, FLOAT3D &v), cdecl
// CEntity
constexpr size_t EN_PLACEMENT = 0x1C;           // en_plPlacement: position, then angles (heading, pitch, banking)
constexpr size_t EN_WORLD = 0x70;               // en_pwoWorld
constexpr size_t EN_VTBL_ENTITY_INFO = 0x98;    // virtual void *GetEntityInfo(void)
constexpr size_t EI_SOURCE_CENTER = 0x08;       // EntityInfo::vSourceCenter
constexpr size_t EN_REFERENCES = 0x18;          // en_ctReferences
// CPlayer
constexpr size_t PL_VIEWPOINT = 0x2C4;          // en_plViewpoint, relative to the player
constexpr size_t PL_3RDPERSONVIEW = 0x32C;      // m_pen3rdPersonView
constexpr size_t PL_VIEWSTATE = 0x330;          // m_iViewState: 3 third person
// CPlayerView
constexpr size_t PV_OWNER = 0x294;              // m_penOwner
constexpr size_t PV_TYPE = 0x2A8;               // m_iViewType
constexpr int VT_3RDPERSONVIEW = 3;
// CCastRay (Engine.dll)
constexpr size_t CR_SIZE = 0x100;               // at most 0x88 in this build
constexpr size_t CR_HIT_TRANSLUCENT = 0x58;     // cr_bHitTranslucentPortals
constexpr size_t CR_HIT_MODELS = 0x5C;          // cr_ttHitModels
constexpr size_t CR_PEN_HIT = 0x6C;             // cr_penHit
constexpr size_t CR_HIT_POINT = 0x70;           // cr_vHit
constexpr size_t CR_HIT_POLYGON = 0x80;         // cr_pbpoBrushPolygon: its plane pointer first, the plane's normal at +4
constexpr int TT_COLLISIONBOX = 2;

constexpr float PITCH_DOWN = 10.0f;
constexpr float DISTANCE = 5.75f;
constexpr float AIM_RANGE = 2000.0f;
constexpr float AIM_MISS_DISTANCE = 50.0f;
constexpr float MIN_VIEWER_DISTANCE = 2.0f;
constexpr float EASE_OUT = 0.3f;                // part of the way back out per tick

using PostMoving_t = void(__fastcall *)(void *pv, void *pvEdx);
using GetViewEntity_t = void **(__fastcall *)(void *pen, void *pvEdx, void **ppenResult);
using CastRayConstruct_t = void *(__fastcall *)(void *pcr, void *pvEdx, void *penOrigin, const float *pvOrigin, const float *pvTarget);
using WorldCastRay_t = void(__fastcall *)(void *pwo, void *pvEdx, void *pcr);
using SetPlacement_t = void(__fastcall *)(void *pen, void *pvEdx, const float *ppl);
using RelativeToAbsolute_t = void(__fastcall *)(float *ppl, void *pvEdx, const float *pplFrame);
using MakeRotationMatrix_t = void(__cdecl *)(float *pm, const float *pa);
using DecomposeRotationMatrix_t = void(__cdecl *)(float *pa, const float *pm);
using EntityInfoPosition_t = void(__cdecl *)(void *pen, const float *pfOffset, float *pv);
using GetEntityInfo_t = void *(__fastcall *)(void *pen, void *pvEdx);

PostMoving_t _pfnPostMoving = nullptr;
GetViewEntity_t _pfnGetViewEntity = nullptr;
CastRayConstruct_t _pfnCastRayConstruct = nullptr;
WorldCastRay_t _pfnWorldCastRay = nullptr;
SetPlacement_t _pfnSetPlacement = nullptr;
RelativeToAbsolute_t _pfnRelativeToAbsolute = nullptr;
MakeRotationMatrix_t _pfnMakeRotationMatrix = nullptr;
DecomposeRotationMatrix_t _pfnDecomposeRotationMatrix = nullptr;
EntityInfoPosition_t _pfnEntityInfoPosition = nullptr;

struct Vec {
  float x, y, z;
  Vec operator+(const Vec &v) const { return { x + v.x, y + v.y, z + v.z }; }
  Vec operator-(const Vec &v) const { return { x - v.x, y - v.y, z - v.z }; }
  Vec operator*(float f) const { return { x * f, y * f, z * f }; }
  float Dot(const Vec &v) const { return x * v.x + y * v.y + z * v.z; }
  Vec Cross(const Vec &v) const { return { y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x }; }
  float Length() const { return sqrtf(Dot(*this)); }
  Vec Normalized() const { float f = Length(); return f > 0.0f ? *this * (1.0f / f) : *this; }
};

// Column j of a row-major FLOATmatrix3D: 0 right, 1 up, 2 back.
Vec Column(const float *m, int j) { return { m[j], m[3 + j], m[6 + j] }; }

// A ray like the game's targeting ray: through translucent portals, models by
// their collision boxes, never hitting penOrigin.  Returns whether it hit.
bool CastRay(void *penOrigin, const Vec &vFrom, const Vec &vTo, Vec &vHit, const float **ppfNormal)
{
  alignas(16) BYTE aubRay[CR_SIZE] = {};
  _pfnCastRayConstruct(aubRay, nullptr, penOrigin, &vFrom.x, &vTo.x);
  *(int *)(aubRay + CR_HIT_TRANSLUCENT) = 0;
  *(int *)(aubRay + CR_HIT_MODELS) = TT_COLLISIONBOX;
  _pfnWorldCastRay(*(void **)((char *)penOrigin + EN_WORLD), nullptr, aubRay);
  if (*(void **)(aubRay + CR_PEN_HIT) == nullptr) return false;
  memcpy(&vHit, aubRay + CR_HIT_POINT, sizeof(vHit));
  if (ppfNormal != nullptr) {
    char *pbpo = *(char **)(aubRay + CR_HIT_POLYGON);
    *ppfNormal = pbpo != nullptr ? (const float *)(*(char **)pbpo + 4) : nullptr;
  }
  return true;
}

// The camera's distance at the last tick, per view entity.
struct ViewDistance {
  void *pv;
  float fDistance;
};
ViewDistance _aViewDistances[8] = {};
int _iNextViewDistance = 0;

float &LastDistance(void *pv)
{
  for (ViewDistance &vd : _aViewDistances) {
    if (vd.pv == pv) return vd.fDistance;
  }
  ViewDistance &vd = _aViewDistances[_iNextViewDistance++ % 8];
  vd = { pv, DISTANCE };
  return vd.fDistance;
}

// The point the camera backs off from: the player's EntityInfo source centre.
Vec PlayerCentre(char *penPlayer)
{
  void *pei = (*(GetEntityInfo_t **)penPlayer)[EN_VTBL_ENTITY_INFO / sizeof(void *)](penPlayer, nullptr);
  Vec v;
  _pfnEntityInfoPosition(penPlayer, pei != nullptr ? (const float *)((char *)pei + EI_SOURCE_CENTER) : nullptr, &v.x);
  return v;
}

void SetCameraPosition(char *pv, char *penPlayer)
{
  const float *plPlayer = (const float *)(penPlayer + EN_PLACEMENT);
  // the eyes, and the camera's angles: the view, a bit down, no banking
  float plEyes[6], plCamera[6];
  memcpy(plEyes, penPlayer + PL_VIEWPOINT, sizeof(plEyes));
  memcpy(plCamera, plEyes, sizeof(plCamera));
  _pfnRelativeToAbsolute(plEyes, nullptr, plPlayer);
  plCamera[4] -= PITCH_DOWN;
  plCamera[5] = 0.0f;
  _pfnRelativeToAbsolute(plCamera, nullptr, plPlayer);

  // back off from the player's centre
  Vec vBase = PlayerCentre(penPlayer);
  float m[9];
  _pfnMakeRotationMatrix(m, plCamera + 3);
  Vec vRight = Column(m, 0), vUp = Column(m, 1), vBack = Column(m, 2);
  const Vec avOffsets[5] = { vUp, vUp * -1.0f, vRight, vRight * -1.0f, { 0, 0, 0 } };
  float fDistance = DISTANCE, fBackOff = 0.0f;
  for (const Vec &vOffset : avOffsets) {
    Vec vDest = vBase + vBack * DISTANCE + vOffset, vHit;
    const float *pfNormal = nullptr;
    if (!CastRay(penPlayer, vBase, vDest, vHit, &pfNormal)) continue;
    fDistance = std::fmin(fDistance, (vHit - vBase).Length() - 0.5f);
    if (pfNormal != nullptr) {
      Vec vNormal = { pfNormal[0], pfNormal[1], pfNormal[2] };
      fBackOff = std::fmax(fBackOff, fabsf(vNormal.Dot((vDest - vBase).Normalized())) * 0.25f);
    }
  }
  fDistance = std::fmax(fDistance - fBackOff, 0.0f);
  float &fLastDistance = LastDistance(pv);
  if (fDistance > fLastDistance) fDistance = fLastDistance + (fDistance - fLastDistance) * EASE_OUT;
  fLastDistance = fDistance;
  Vec vCamera = vBase + vBack * fDistance;

  // what the eyes look at
  float mEyes[9];
  _pfnMakeRotationMatrix(mEyes, plEyes + 3);
  Vec vEyes = { plEyes[0], plEyes[1], plEyes[2] }, vAhead = Column(mEyes, 2) * -1.0f, vTarget;
  if (!CastRay(penPlayer, vEyes, vEyes + vAhead * AIM_RANGE, vTarget, nullptr)) vTarget = vEyes + vAhead * AIM_MISS_DISTANCE;

  // look at it
  Vec vZ = vCamera - vTarget;
  vZ = vZ.Length() > 0.01f ? vZ.Normalized() : Vec{ 0, 1, 0 };
  Vec vY = vUp;
  if (fabsf(vY.Dot(vZ)) > 0.9f) vY = vBack * -1.0f;
  Vec vX = vY.Cross(vZ).Normalized();
  vY = vZ.Cross(vX).Normalized();
  const float mLook[9] = { vX.x, vY.x, vZ.x, vX.y, vY.y, vZ.y, vX.z, vY.z, vZ.z };
  float plNew[6] = { vCamera.x, vCamera.y, vCamera.z };
  _pfnDecomposeRotationMatrix(plNew + 3, mLook);
  _pfnSetPlacement(pv, nullptr, plNew);
}

void __fastcall PostMoving_Hook(char *pv, void *pvEdx)
{
  char *penPlayer = *(char **)(pv + PV_OWNER);
  if (g_cfg.bTfeThirdPerson && *(int *)(pv + PV_TYPE) == VT_3RDPERSONVIEW && penPlayer != nullptr) {
    SetCameraPosition(pv, penPlayer);
    return;
  }
  _pfnPostMoving(pv, pvEdx);
}

void **__fastcall GetViewEntity_Hook(char *penPlayer, void *pvEdx, void **ppenResult)
{
  char *pv = *(char **)(penPlayer + PL_3RDPERSONVIEW);
  if (g_cfg.bTfeThirdPerson && *(int *)(penPlayer + PL_VIEWSTATE) == VT_3RDPERSONVIEW && pv != nullptr
      && *(int *)(pv + PV_TYPE) == VT_3RDPERSONVIEW) {
    const float *pfCamera = (const float *)(pv + EN_PLACEMENT);
    if ((Vec{ pfCamera[0], pfCamera[1], pfCamera[2] } - PlayerCentre(penPlayer)).Length() <= MIN_VIEWER_DISTANCE) {
      *ppenResult = penPlayer;   // a CEntityPointer: holds a reference
      (*(int *)(penPlayer + EN_REFERENCES))++;
      return ppenResult;
    }
  }
  return _pfnGetViewEntity(penPlayer, pvEdx, ppenResult);
}

}  // namespace

void InstallThirdPersonEntitiesHooks()
{
  _pfnCastRayConstruct = (CastRayConstruct_t)EngineProc("??0CCastRay@@QAE@PAVCEntity@@ABV?$Vector@M$02@@1@Z");
  _pfnWorldCastRay = (WorldCastRay_t)EngineProc("?CastRay@CWorld@@QAEXAAVCCastRay@@@Z");
  _pfnSetPlacement = (SetPlacement_t)EngineProc("?SetPlacement@CEntity@@QAEXABVCPlacement3D@@@Z");
  _pfnRelativeToAbsolute = (RelativeToAbsolute_t)EngineProc("?RelativeToAbsolute@CPlacement3D@@QAEXABV1@@Z");
  _pfnMakeRotationMatrix = (MakeRotationMatrix_t)EngineProc("?MakeRotationMatrixFast@@YAXAAV?$Matrix@M$02$02@@ABV?$Vector@M$02@@@Z");
  _pfnDecomposeRotationMatrix =
      (DecomposeRotationMatrix_t)EngineProc("?DecomposeRotationMatrixNoSnap@@YAXAAV?$Vector@M$02@@ABV?$Matrix@M$02$02@@@Z");
  _pfnEntityInfoPosition = (EntityInfoPosition_t)RebaseEntities(ENT_ENTITY_INFO_POSITION);
  // push esi; mov esi, ecx; mov eax, [esi + 0x2A8]; cmp eax, 3
  static const BYTE aubPostMoving[] = { 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0xA8, 0x02, 0x00, 0x00, 0x83, 0xF8, 0x03 };
  // push ebp; mov ebp, esp; sub esp, 0x3C; mov eax, [ebp + 8]
  static const BYTE aubInfoPosition[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x3C, 0x8B, 0x45, 0x08 };
  // push ebp; mov ebp, esp; push ecx; mov eax, [ecx + 0x330]
  static const BYTE aubGetViewEntity[] = { 0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x81, 0x30, 0x03, 0x00, 0x00 };
  uintptr_t ulPostMoving = RebaseEntities(ENT_VIEW_POSTMOVING), ulGetViewEntity = RebaseEntities(ENT_GET_VIEW_ENTITY);
  bool bOk = _pfnCastRayConstruct && _pfnWorldCastRay && _pfnSetPlacement && _pfnRelativeToAbsolute && _pfnMakeRotationMatrix
          && _pfnDecomposeRotationMatrix && CodeMatches(ulPostMoving, aubPostMoving, sizeof(aubPostMoving))
          && CodeMatches(RebaseEntities(ENT_ENTITY_INFO_POSITION), aubInfoPosition, sizeof(aubInfoPosition))
          && CodeMatches(ulGetViewEntity, aubGetViewEntity, sizeof(aubGetViewEntity))
          && HookFunction((void *)ulPostMoving, (void *)PostMoving_Hook, (void **)&_pfnPostMoving, "CPlayerView::PostMoving")
          && HookFunction((void *)ulGetViewEntity, (void *)GetViewEntity_Hook, (void **)&_pfnGetViewEntity, "CPlayer::GetViewEntity");
  Log("Third-person camera: %s", bOk ? "installed" : "code differs, not installed");
}
