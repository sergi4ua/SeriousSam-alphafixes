// The cannon.
//
// The alpha's Entities.dll has the whole cannon: the weapon (CPlayerWeapons
// type 16, key 9), its cannonball ammo, the cannonball projectile, the weapon
// and ammo items and the HUD icons.  It is only locked out of the player's
// weapon set (CPlayerWeapons +0xEC, bit n-1 for weapon type n):
//  - picking up a weapon item adds its bit and then clears bit 15, the cannon's
//    (and dh, 0x7F at 0x6021ADA6), so the cannon item gives nothing;
//  - the weapons a player marker gives at the start of a level get the same
//    treatment (and ah, 0x7F at 0x6021A80F);
//  - plrCheat_bGiveAll sets the set to 0x6BFF, every weapon but the cannon.
// Unlocking it removes the two masks and adds bit 15 to the cheat's set.
#include "alphafixes.h"

namespace {

// Entities.dll (13 Jan 2000 build, preferred base)
constexpr uintptr_t ENT_WEAPONITEM_MASK = 0x6021AD9E;   // CPlayerWeapons, receiving a weapon item
constexpr uintptr_t ENT_MARKER_MASK = 0x6021A806;       // CPlayerWeapons, weapons from the player marker
constexpr uintptr_t ENT_GIVEALL_SET = 0x6021AA00;       // CPlayerWeapons, plrCheat_bGiveAll

bool Patch(uintptr_t ulAddress, const BYTE *pubOld, const BYTE *pubNew, size_t ctBytes)
{
  uintptr_t ul = RebaseEntities(ulAddress);
  return CodeMatches(ul, pubOld, ctBytes) && WriteCode((void *)ul, pubNew, ctBytes);
}

}  // namespace

void InstallCannonEntitiesPatches()
{
  if (!g_cfg.bUnlockCannon) return;

  // mov edx, [esi + 0xEC]; mov ecx, esi; and dh, 0x7F; mov [esi + 0xEC], edx
  static const BYTE aubItemOld[] = { 0x8B, 0x96, 0xEC, 0x00, 0x00, 0x00, 0x8B, 0xCE, 0x80, 0xE6, 0x7F,
                                     0x89, 0x96, 0xEC, 0x00, 0x00, 0x00 };
  static const BYTE aubItemNew[] = { 0x8B, 0x96, 0xEC, 0x00, 0x00, 0x00, 0x8B, 0xCE, 0x90, 0x90, 0x90,
                                     0x89, 0x96, 0xEC, 0x00, 0x00, 0x00 };
  // and eax, ecx; or eax, edi; mov edi, 1; and ah, 0x7F; or al, 3
  static const BYTE aubMarkerOld[] = { 0x23, 0xC1, 0x0B, 0xC7, 0xBF, 0x01, 0x00, 0x00, 0x00, 0x80, 0xE4, 0x7F, 0x0C, 0x03 };
  static const BYTE aubMarkerNew[] = { 0x23, 0xC1, 0x0B, 0xC7, 0xBF, 0x01, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0x0C, 0x03 };
  // mov dword ptr [ecx + 0xEC], 0x6BFF -> 0xEBFF
  static const BYTE aubGiveAllOld[] = { 0xC7, 0x81, 0xEC, 0x00, 0x00, 0x00, 0xFF, 0x6B, 0x00, 0x00 };
  static const BYTE aubGiveAllNew[] = { 0xC7, 0x81, 0xEC, 0x00, 0x00, 0x00, 0xFF, 0xEB, 0x00, 0x00 };

  bool bOk = Patch(ENT_WEAPONITEM_MASK, aubItemOld, aubItemNew, sizeof(aubItemOld));
  bOk &= Patch(ENT_MARKER_MASK, aubMarkerOld, aubMarkerNew, sizeof(aubMarkerOld));
  bOk &= Patch(ENT_GIVEALL_SET, aubGiveAllOld, aubGiveAllNew, sizeof(aubGiveAllOld));
  Log("Cannon unlock: %s", bOk ? "installed" : "failed");
}
