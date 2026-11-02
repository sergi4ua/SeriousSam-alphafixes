// Links CEnemyBase (SDK/Entities/EnemyBase.h) to the alpha's own code in
// Entities.dll, which exports none of it.
//
// Virtual functions jump through the original CEnemyBase vtable, so they reach
// exactly the code the alpha's own enemies use.  The few non-virtual helpers
// are called at their address in the 13 Jan 2000 build.  InitEnemyBase()
// checks that build and the first bytes of every helper before anything is
// used; on a different Entities.dll the class doesn't load.
#include "StdH.h"
#include <Entities/EnemyBase.h>
#include "AlphaFixesEntities.h"

namespace {

constexpr uintptr_t ENEMYBASE_VTABLE_RVA = 0xAFFB0;

struct CodeRef {
  uintptr_t ulRVA;
  UBYTE aubStart[6];   // the first bytes, before any relocated address
  void **ppfn;
};

}  // namespace

// used by the thunks below, so not static
void **_apvEnemyBaseVtable = nullptr;
void *_pfnApplyStretch = nullptr;
void *_pfnSpawnWatcher = nullptr;
void *_pfnWatcherSetParameters = nullptr;
void *_pfnStopMoving = nullptr;
void *_pfnShootProjectile = nullptr;
void *_pfnPreparePropelledProjectile = nullptr;
void *_pfnGetEntityInfoPosition = nullptr;

bool InitEnemyBase(char *strError, size_t ctError)
{
  HMODULE hEntities = GetModuleHandleA("Entities.dll");
  if (hEntities == nullptr) {
    snprintf(strError, ctError, "Entities.dll is not loaded");
    return false;
  }
  if (ModuleTimestamp(hEntities) != ENTITIES_TIMESTAMP) {
    snprintf(strError, ctError, "Entities.dll is not the alpha's build of 13 Jan 2000");
    return false;
  }
  uintptr_t ulBase = (uintptr_t)hEntities;

  static const CodeRef acr[] = {
    { 0x33E60, { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C }, &_pfnApplyStretch },
    { 0x33CA0, { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68 }, &_pfnSpawnWatcher },
    { 0x95760, { 0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08 }, &_pfnWatcherSetParameters },
    { 0x341E0, { 0x56, 0x8B, 0xF1, 0xE8, 0x18, 0x00 }, &_pfnStopMoving },
    { 0x34790, { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68 }, &_pfnShootProjectile },
    { 0x344B0, { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xB0 }, &_pfnPreparePropelledProjectile },
    { 0x01A60, { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x3C }, &_pfnGetEntityInfoPosition },
  };
  for (const CodeRef &cr : acr) {
    const UBYTE *pub = (const UBYTE *)(ulBase + cr.ulRVA);
    if (memcmp(pub, cr.aubStart, sizeof(cr.aubStart)) != 0) {
      snprintf(strError, ctError, "unexpected code in Entities.dll at RVA %05X", (unsigned)cr.ulRVA);
      return false;
    }
    *cr.ppfn = (void *)pub;
  }

  // The vtable must be the one CEnemyBase's constructor installs: its
  // SetDefaultProperties slot is the exported function.
  void **apv = (void **)(ulBase + ENEMYBASE_VTABLE_RVA);
  void *pvSetDefaults = (void *)GetProcAddress(hEntities, "?SetDefaultProperties@CEnemyBase@@UAEXXZ");
  if (pvSetDefaults == nullptr || apv[5] != pvSetDefaults) {
    snprintf(strError, ctError, "CEnemyBase's vtable is not where expected in Entities.dll");
    return false;
  }
  _apvEnemyBaseVtable = apv;
  return true;
}

#ifdef AFE_TRACE
// --- test builds: event trace (Bin\AlphaFixesEntities_trace.log) ---------------

static float CurrentTick(void)
{
  typedef float (__thiscall *CurrentTick_t)(void *ptimer);
  static void **pptimer = (void **)GetProcAddress(GetModuleHandleA("Engine.dll"), "?_pTimer@@3PAVCTimer@@A");
  static CurrentTick_t pfn = (CurrentTick_t)GetProcAddress(GetModuleHandleA("Engine.dll"), "?CurrentTick@CTimer@@QBE?BMXZ");
  return (pptimer && *pptimer && pfn) ? pfn(*pptimer) : -1.0f;
}

BOOL CEnemyBase::HandleEvent(const CEntityEvent &ee)
{
  static FILE *pf = fopen("AlphaFixesEntities_trace.log", "a");
  if (pf) {
    fprintf(pf, "%8.2f %p event %08lX  health %.0f enemy %p watcher %p/%p\n", CurrentTick(), (void *)this,
      ee.ee_slEvent, en_fHealth, (void *)m_penEnemy.ep_pen, (void *)m_penWatcher.ep_pen, (void *)m_pWatcher);
    fflush(pf);
  }
  return CRationalEntity::HandleEvent(ee);
}
#endif

// --- thunks ------------------------------------------------------------------

// Jumps to slot iSlot of the original vtable, with this and the arguments as
// they are.
#define ENEMYBASE_VIRTUAL(iSlot)               \
  __asm mov eax, _apvEnemyBaseVtable           \
  __asm jmp dword ptr [eax + 4 * (iSlot)]

__declspec(naked) void CEnemyBase::Read_t(CTStream *) { ENEMYBASE_VIRTUAL(2) }
__declspec(naked) void CEnemyBase::Precache(void) { ENEMYBASE_VIRTUAL(4) }
__declspec(naked) const CTString &CEnemyBase::GetName(void) const { ENEMYBASE_VIRTUAL(13) }
__declspec(naked) const CTString &CEnemyBase::GetDescription(void) const { ENEMYBASE_VIRTUAL(14) }
__declspec(naked) BOOL CEnemyBase::IsTargetable(void) const { ENEMYBASE_VIRTUAL(16) }
__declspec(naked) BOOL CEnemyBase::MovesByTargetedRoute(CTString &) const { ENEMYBASE_VIRTUAL(18) }
__declspec(naked) BOOL CEnemyBase::DropsMarker(CTFileName &, CTString &) const { ENEMYBASE_VIRTUAL(19) }
__declspec(naked) BOOL CEnemyBase::AdjustShadingParameters(FLOAT3D &, COLOR &, COLOR &) { ENEMYBASE_VIRTUAL(30) }
__declspec(naked) void CEnemyBase::ReceiveDamage(CEntity *, enum DamageType, FLOAT, const FLOAT3D &, const FLOAT3D &) { ENEMYBASE_VIRTUAL(36) }
__declspec(naked) BOOL CEnemyBase::FillEntityStatistics(struct EntityStats *) { ENEMYBASE_VIRTUAL(39) }

__declspec(naked) void CEnemyBase::Virtual45_BlowUp(void) { ENEMYBASE_VIRTUAL(45) }
__declspec(naked) void CEnemyBase::Virtual46(void) { ENEMYBASE_VIRTUAL(46) }
__declspec(naked) void CEnemyBase::Virtual47_SpawnEffect(void) { ENEMYBASE_VIRTUAL(47) }
__declspec(naked) void CEnemyBase::Virtual48_SoundEvent(INDEX) { ENEMYBASE_VIRTUAL(48) }
__declspec(naked) void CEnemyBase::Virtual49_SoundEvent(void) { ENEMYBASE_VIRTUAL(49) }
__declspec(naked) void CEnemyBase::StandingAnim(void) { ENEMYBASE_VIRTUAL(50) }
__declspec(naked) void CEnemyBase::WalkingAnim(void) { ENEMYBASE_VIRTUAL(51) }
__declspec(naked) void CEnemyBase::RunningAnim(void) { ENEMYBASE_VIRTUAL(52) }
__declspec(naked) void CEnemyBase::RotatingAnim(void) { ENEMYBASE_VIRTUAL(53) }
__declspec(naked) void CEnemyBase::ChargeAnim(void) { ENEMYBASE_VIRTUAL(54) }
__declspec(naked) INDEX CEnemyBase::AnimForDamage(FLOAT) { ENEMYBASE_VIRTUAL(55) }
__declspec(naked) void CEnemyBase::BlowUpNotify(void) { ENEMYBASE_VIRTUAL(56) }
__declspec(naked) INDEX CEnemyBase::AnimForDeath(void) { ENEMYBASE_VIRTUAL(57) }
__declspec(naked) void CEnemyBase::DeathNotify(void) { ENEMYBASE_VIRTUAL(58) }
__declspec(naked) void CEnemyBase::IdleSound(void) { ENEMYBASE_VIRTUAL(59) }
__declspec(naked) void CEnemyBase::SightSound(void) { ENEMYBASE_VIRTUAL(60) }
__declspec(naked) void CEnemyBase::WoundSound(void) { ENEMYBASE_VIRTUAL(61) }
__declspec(naked) void CEnemyBase::DeathSound(void) { ENEMYBASE_VIRTUAL(62) }

__declspec(naked) void CEnemyBase::ApplyStretch(void)
{
  __asm jmp dword ptr [_pfnApplyStretch]
}
__declspec(naked) void CEnemyBase::SpawnWatcher(void)
{
  __asm jmp dword ptr [_pfnSpawnWatcher]
}
__declspec(naked) void CWatcher::SetParameters(FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT)
{
  __asm jmp dword ptr [_pfnWatcherSetParameters]
}
__declspec(naked) void CEnemyBase::StopMoving(void)
{
  __asm jmp dword ptr [_pfnStopMoving]
}
__declspec(naked) CEntityPointer CEnemyBase::ShootProjectile(enum ProjectileType, const FLOAT3D &, const ANGLE3D &)
{
  __asm jmp dword ptr [_pfnShootProjectile]
}
__declspec(naked) void CEnemyBase::PreparePropelledProjectile(CPlacement3D &, const FLOAT3D &, const ANGLE3D &)
{
  __asm jmp dword ptr [_pfnPreparePropelledProjectile]
}
__declspec(naked) void GetEntityInfoPosition(CEntity *, FLOAT *, FLOAT3D &)
{
  __asm jmp dword ptr [_pfnGetEntityInfoPosition]
}
