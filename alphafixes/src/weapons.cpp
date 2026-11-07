// Weapon switching with the mouse wheel, and a weapon bar in the HUD.
//
// The alpha selects weapons only by number keys.  Its controls put the number
// of the pressed select key (plrControls_bSelectWeapon[1..29]) in the low five
// bits of the action's buttons; the player sends it to its CPlayerWeapons as
// ESelectWeapon, which calls SelectWeapon(key) (Entities.dll 0x6021BA10).
// Keys 1-9 pick a group (colt and double colt share 2, and so on); the other
// numbers are unused.
//  - Wheel notches (Raw Input) go into the next actions as 27 (next weapon) or
//    28 (previous weapon), one per action, unless a select key is pressed.
//  - SelectWeapon handles 27 and 28: from the weapon being switched to (+0xE0),
//    the next or previous weapon the player has (+0xEC) and has ammo for
//    (0x6021B1F0) becomes the one to switch to, as the original does for keys.
//    The selection happens in the game's own logic, so demos and other
//    players see it.
//  - After CPlayer::RenderHUD (0x6020DE40), while the weapon being switched to
//    changed in the last moment, a row of the player's weapons is drawn above
//    the HUD's bottom bar with the HUD's own icons, the selected one marked.
//    Two styles ([Weapons] WeaponBar): Tiles puts each icon on a dark tile,
//    the selected one green, sized by the HUD scale; Clean draws the icons
//    alone with a soft shadow, the selected one yellow and larger, sized by the
//    screen height (a 480-line screen is the original size) so it looks the
//    same at any resolution and aspect ratio.
#include "alphafixes.h"

namespace {

// Entities.dll (13 Jan 2000 build, preferred base)
constexpr uintptr_t ENT_SELECT_WEAPON = 0x6021BA10;     // CPlayerWeapons::SelectWeapon(INDEX iKey)
constexpr uintptr_t ENT_HAS_AMMO = 0x6021B1F0;          // BOOL CPlayerWeapons::HasAmmo(INDEX iWeapon)
constexpr uintptr_t ENT_RENDER_HUD = 0x6020DE40;        // void CPlayer::RenderHUD(void), draws on _pDP
constexpr uintptr_t ENT_HUD_DRAWPORT = 0x6029AC0C;      // CDrawPort *_pDP
constexpr uintptr_t ENT_HUD_SCALE = 0x6029ABFC;         // FLOAT _fHUDScale
constexpr uintptr_t ENT_HUD_WIDTH = 0x6029AC08;         // PIX _pixDPWidth
constexpr uintptr_t ENT_HUD_HEIGHT = 0x6029AC04;        // PIX _pixDPHeight
constexpr size_t PL_WEAPONS = 0x320;                    // CPlayer: its CPlayerWeapons
constexpr size_t PW_CURRENT = 0xDC, PW_WANTED = 0xE0, PW_CHANGE = 0xE8, PW_AVAILABLE = 0xEC;
constexpr size_t PA_BUTTONS = 0x24;                     // CPlayerAction
constexpr int KEY_NEXT = 27, KEY_PREVIOUS = 28;
constexpr int CT_WEAPONS = 16;                          // weapon types 1..16; 11 and 13 don't exist

// The HUD's icon for each weapon type (Player class texture components), as CPlayer::RenderHUD picks them.
const long _aulIconComponent[CT_WEAPONS + 1] = {
  0, 0x19124, 0x1911F, 0x1911F, 0x19129, 0x19120, 0x1912A, 0x19126, 0x19128,
  0x19123, 0x19127, 0, 0x19121, 0, 0x19125, 0x19122, 0x1911E,
};

using SelectWeapon_t = void(__fastcall *)(char *ppw, void *pvEdx, int iKey);
using HasAmmo_t = BOOL(__fastcall *)(char *ppw, void *pvEdx, int iWeapon);
using RenderHUD_t = void(__fastcall *)(char *pen, void *pvEdx);
using GetTextureData_t = void *(__fastcall *)(char *pen, void *pvEdx, long ulComponent);
using Construct_t = void(__fastcall *)(void *p, void *pvEdx);
using SetData_t = void(__fastcall *)(void *pao, void *pvEdx, void *pad);
using PutTexture_t = void(__fastcall *)(void *pdp, void *pvEdx, void *pto, const long *pbox, ULONG col);
using Fill_t = void(__fastcall *)(void *pdp, void *pvEdx, long x, long y, long w, long h, ULONG col);

SelectWeapon_t _pfnSelectWeapon = nullptr;
HasAmmo_t _pfnHasAmmo = nullptr;
RenderHUD_t _pfnRenderHUD = nullptr;
GetTextureData_t _pfnGetTextureData = nullptr;
SetData_t _pfnSetData = nullptr;
PutTexture_t _pfnPutTexture = nullptr;
Fill_t _pfnFill = nullptr;

volatile LONG _slWheel = 0;                  // wheel movement not yet turned into actions (120 per notch)

bool Exists(int iWeapon) { return iWeapon >= 1 && iWeapon <= CT_WEAPONS && _aulIconComponent[iWeapon] != 0; }
bool Has(char *ppw, int iWeapon) { return (*(ULONG *)(ppw + PW_AVAILABLE) >> (iWeapon - 1)) & 1; }

void __fastcall SelectWeapon_Hook(char *ppw, void *pvEdx, int iKey)
{
  if (iKey != KEY_NEXT && iKey != KEY_PREVIOUS) {
    _pfnSelectWeapon(ppw, pvEdx, iKey);
    return;
  }
  int iStep = iKey == KEY_NEXT ? 1 : -1;
  int iWanted = *(int *)(ppw + PW_WANTED);
  int iWeapon = iWanted;
  for (int i = 0; i < CT_WEAPONS; i++) {
    iWeapon = (iWeapon - 1 + iStep + CT_WEAPONS) % CT_WEAPONS + 1;
    if (iWeapon == iWanted) return;
    if (Exists(iWeapon) && Has(ppw, iWeapon) && _pfnHasAmmo(ppw, nullptr, iWeapon)) {
      *(int *)(ppw + PW_WANTED) = iWeapon;
      *(int *)(ppw + PW_CHANGE) = 1;
      return;
    }
  }
}

// --- weapon bar ---------------------------------------------------------------------

alignas(16) BYTE _aubIcons[CT_WEAPONS + 1][0x40];    // CTextureObjects
void *_apvIconData[CT_WEAPONS + 1];
bool _bIconsMade = false;
char *_penLastPlayer = nullptr;
int _iLastWanted = -1;
DWORD _tmShown = 0;

constexpr DWORD BAR_SHOW_MS = 1500, BAR_FADE_MS = 400;

ULONG WithAlpha(ULONG col, float fAlpha)
{
  ULONG a = (ULONG)((col & 0xFF) * fAlpha);
  return (col & 0xFFFFFF00) | (a > 255 ? 255 : a);
}

void PutIcon(char *pen, void *pdp, int iWeapon, long x, long y, long pixSize, ULONG col)
{
  void *ptd = _pfnGetTextureData(pen, nullptr, _aulIconComponent[iWeapon]);
  if (ptd == nullptr) return;
  if (ptd != _apvIconData[iWeapon]) {
    _pfnSetData(_aubIcons[iWeapon], nullptr, ptd);
    _apvIconData[iWeapon] = ptd;
  }
  const long abox[4] = { x, y, x + pixSize, y + pixSize };   // PIXaabbox2D: min, max
  _pfnPutTexture(pdp, nullptr, _aubIcons[iWeapon], abox, col);
}

// The Clean style: no tiles, a shadow under each icon, the selected icon yellow and larger.
void DrawCleanBar(char *pen, char *ppw, void *pdp, const int *aiWeapons, int ctWeapons, int iWanted,
                  long pixW, long pixH, float fAlpha)
{
  float fScale = pixH / 480.0f;
  // narrower than the row (all weapons, below 5:4): shrink it to fit
  float fRowWidth = (ctWeapons - 1) * (28.0f + 10.0f) + 36.0f;
  if (fRowWidth * fScale > pixW * 0.96f) fScale = pixW * 0.96f / fRowWidth;
  long pixIcon = (long)(28.0f * fScale + 0.5f);
  long pixSelected = (long)(36.0f * fScale + 0.5f);
  long pixGap = (long)(10.0f * fScale + 0.5f);
  long pixShadow = (long)(1.5f * fScale + 0.5f);
  if (pixShadow < 1) pixShadow = 1;
  long pixTotal = (ctWeapons - 1) * (pixIcon + pixGap) + pixSelected;
  long x = (pixW - pixTotal) / 2;
  long yBottom = pixH - (long)(44.0f * fScale + 0.5f);   // above the HUD's bottom bar
  for (int i = 0; i < ctWeapons; i++) {
    int iWeapon = aiWeapons[i];
    bool bSelected = iWeapon == iWanted;
    long pixSize = bSelected ? pixSelected : pixIcon;
    long y = yBottom - pixSize;
    ULONG col = bSelected ? 0xFFD830FF : _pfnHasAmmo(ppw, nullptr, iWeapon) ? 0xE0E0E0B0 : 0xFF505090;
    PutIcon(pen, pdp, iWeapon, x + pixShadow, y + pixShadow, pixSize, WithAlpha(0x00000090, fAlpha));
    PutIcon(pen, pdp, iWeapon, x, y, pixSize, WithAlpha(col, fAlpha));
    x += pixSize + pixGap;
  }
}

void DrawWeaponBar(char *pen)
{
  char *ppw = *(char **)(pen + PL_WEAPONS);
  void *pdp = *(void **)RebaseEntities(ENT_HUD_DRAWPORT);
  if (ppw == nullptr || pdp == nullptr) return;

  int iWanted = *(int *)(ppw + PW_WANTED);
  DWORD tmNow = GetTickCount();
  if (pen != _penLastPlayer) {   // a new player (level start): no bar for the weapon it starts with
    _penLastPlayer = pen;
    _iLastWanted = iWanted;
    _tmShown = tmNow - BAR_SHOW_MS - BAR_FADE_MS;
  }
  if (iWanted != _iLastWanted) {
    _iLastWanted = iWanted;
    _tmShown = tmNow;
  }
  DWORD tmSince = tmNow - _tmShown;
  if (tmSince >= BAR_SHOW_MS + BAR_FADE_MS) return;
  float fAlpha = tmSince <= BAR_SHOW_MS ? 1.0f : 1.0f - (float)(tmSince - BAR_SHOW_MS) / BAR_FADE_MS;

  int aiWeapons[CT_WEAPONS];
  int ctWeapons = 0;
  for (int i = 1; i <= CT_WEAPONS; i++) {
    if (Exists(i) && Has(ppw, i)) aiWeapons[ctWeapons++] = i;
  }
  if (ctWeapons == 0) return;

  long pixW = *(long *)RebaseEntities(ENT_HUD_WIDTH), pixH = *(long *)RebaseEntities(ENT_HUD_HEIGHT);
  if (g_cfg.iWeaponBar == WEAPONBAR_CLEAN) {
    DrawCleanBar(pen, ppw, pdp, aiWeapons, ctWeapons, iWanted, pixW, pixH, fAlpha);
    return;
  }
  float fScale = *(float *)RebaseEntities(ENT_HUD_SCALE);
  long pixIcon = (long)(32.0f * fScale + 0.5f);
  long pixGap = (long)(8.0f * fScale + 0.5f);
  long pixPad = (long)(3.0f * fScale + 0.5f);
  long pixTotal = ctWeapons * pixIcon + (ctWeapons - 1) * pixGap;
  long x = (pixW - pixTotal) / 2;
  long y = pixH - (long)(pixIcon * 2.4f);   // above the HUD's bottom bar, which is one icon high

  for (int i = 0; i < ctWeapons; i++, x += pixIcon + pixGap) {
    int iWeapon = aiWeapons[i];
    bool bSelected = iWeapon == iWanted;
    bool bAmmo = _pfnHasAmmo(ppw, nullptr, iWeapon) != 0;
    _pfnFill(pdp, nullptr, x - pixPad, y - pixPad, pixIcon + 2 * pixPad, pixIcon + 2 * pixPad,
             WithAlpha(bSelected ? 0x7FFF7F00 | 0x70 : 0x00000060, fAlpha));
    ULONG col = bSelected ? 0xFFFFFFFF : bAmmo ? 0xFFFFFFA0 : 0xFF6060A0;
    PutIcon(pen, pdp, iWeapon, x, y, pixIcon, WithAlpha(col, fAlpha));
  }
}

void __fastcall RenderHUD_Hook(char *pen, void *pvEdx)
{
  _pfnRenderHUD(pen, pvEdx);
  if (g_cfg.iWeaponBar != WEAPONBAR_OFF) DrawWeaponBar(pen);
}

}  // namespace

// Raw Input thread: wheel movement while the game has the mouse.
void AddMouseWheel(int iDelta)
{
  if (g_cfg.bWheelWeapons) InterlockedExchangeAdd(&_slWheel, iDelta);
}

// Timer thread, after CControls::CreateAction made a player's action for this tick.
void PutWheelInAction(char *pAction)
{
  LONG sl = _slWheel;
  if (sl > -WHEEL_DELTA && sl < WHEEL_DELTA) return;
  ULONG &ulButtons = *(ULONG *)(pAction + PA_BUTTONS);
  if (ulButtons & 0x1F) return;   // a select key is pressed: it goes first
  bool bUp = sl > 0;
  InterlockedExchangeAdd(&_slWheel, bUp ? -WHEEL_DELTA : WHEEL_DELTA);
  bool bNext = bUp != g_cfg.bWheelInvert;   // by default the wheel up picks the next weapon
  ulButtons |= bNext ? KEY_NEXT : KEY_PREVIOUS;
}

void InstallWeaponsEntitiesHooks()
{
  // push ebp; mov ebp, esp; push esi; mov esi, ecx; push edi; mov eax, [esi + 0xE0]
  static const BYTE aubSelect[] = { 0x55, 0x8B, 0xEC, 0x56, 0x8B, 0xF1, 0x57, 0x8B, 0x86, 0xE0, 0x00, 0x00, 0x00 };
  // push ebp; mov ebp, esp; push -1; push (handler)
  static const BYTE aubHud[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68 };
  _pfnHasAmmo = (HasAmmo_t)RebaseEntities(ENT_HAS_AMMO);
  _pfnGetTextureData = (GetTextureData_t)EngineProc("?GetTextureDataForComponent@CEntity@@QAEPAVCTextureData@@J@Z");
  _pfnSetData = (SetData_t)EngineProc("?SetData@CAnimObject@@QAEXPAVCAnimData@@@Z");
  _pfnPutTexture = (PutTexture_t)EngineProc("?PutTexture@CDrawPort@@QBEXPAVCTextureObject@@ABV?$AABBox@J$01@@K@Z");
  _pfnFill = (Fill_t)EngineProc("?Fill@CDrawPort@@QBEXJJJJK@Z");
  auto pfnTextureObject = (Construct_t)EngineProc("??0CTextureObject@@QAE@XZ");

  bool bSelect = CodeMatches(RebaseEntities(ENT_SELECT_WEAPON), aubSelect, sizeof(aubSelect))
              && HookFunction((void *)RebaseEntities(ENT_SELECT_WEAPON), (void *)SelectWeapon_Hook,
                              (void **)&_pfnSelectWeapon, "CPlayerWeapons::SelectWeapon");
  Log("Mouse wheel weapon switching: %s", bSelect ? "installed" : "failed");

  bool bBar = _pfnGetTextureData != nullptr && _pfnSetData != nullptr && _pfnPutTexture != nullptr && _pfnFill != nullptr
           && pfnTextureObject != nullptr && CodeMatches(RebaseEntities(ENT_RENDER_HUD), aubHud, sizeof(aubHud));
  if (bBar && !_bIconsMade) {
    for (auto &aub : _aubIcons) pfnTextureObject(aub, nullptr);
    _bIconsMade = true;
  }
  bBar = bBar && HookFunction((void *)RebaseEntities(ENT_RENDER_HUD), (void *)RenderHUD_Hook, (void **)&_pfnRenderHUD,
                              "CPlayer::RenderHUD");
  Log("Weapon bar: %s", bBar ? "installed" : "failed");
}
