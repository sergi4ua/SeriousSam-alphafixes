// AlphaFixes pages in the game's own 3D menu (Options > AlphaFixes).
//
// The alpha's menu (SeriousSam.exe) is made of pages (CGameMenu-like, 0x18
// bytes: vtable, gadget list at +4, parent page +0x10, default gadget +0x14)
// holding gadgets, each shown by a 3D item (see Docs\AlphaMenu).  A settings
// row is a label button gadget linked to a value trigger gadget, as on the
// Video options page; Left, Right and Enter on the row step the trigger, which
// calls its change callback (+0x44) with the new index.
//
// When the game sets up the Options page (0x418D70), the mod adds a fourth
// button there, on the main column item the page leaves unused.  It leads to
// an AlphaFixes page of two buttons on the main column (like the Options page)
// for the Graphics and the Mouse and weapons pages.  Those have a title and up
// to six rows; their 3D items are made once by the game's own constructors and
// set-up functions, placed like the Video page's rows, and shared by both.
// Values change the settings at once; leaving a page saves the ini, and a
// changed anti-aliasing setting resets the video mode (a new pixel format).
// The F11 settings window stays.
#include "alphafixes.h"

#include <cmath>

namespace {

// SeriousSam.exe (13 Jan 2000)
constexpr uintptr_t EXE_OPTIONS_INIT = 0x418D70;        // Options page set-up (vtable slot 0)
constexpr uintptr_t EXE_OPTIONS_MENU = 0x436DE8;
constexpr uintptr_t EXE_OPTIONS_FIRST = 0x436D78;       // "Video options" button
constexpr uintptr_t EXE_OPTIONS_LAST = 0x436D08;        // "Audio options" button
constexpr uintptr_t EXE_MAIN_ITEM_1 = 0x43CAA0;         // main column items at y 2.4 and 1.1
constexpr uintptr_t EXE_MAIN_ITEM_2 = 0x43CB88;
constexpr uintptr_t EXE_MAIN_ITEM_4 = 0x43CD58;         // main column item at y -1.5, unused by the Options page
constexpr uintptr_t EXE_TITLE_ITEM = 0x43D010;          // the menus' title item
constexpr uintptr_t EXE_ITEM_LIST = 0x43D2F0;           // CListHead of all 3D items
constexpr uintptr_t EXE_CHANGE_TO_MENU = 0x416340;      // void ChangeToMenu(CGameMenu *)
constexpr uintptr_t EXE_MENU_CTOR = 0x416380;
constexpr uintptr_t EXE_MENU_NOTHING = 0x4163B0;        // empty virtual
constexpr uintptr_t EXE_MENU_START = 0x4163C0;
constexpr uintptr_t EXE_MENU_END = 0x4163E0;
constexpr uintptr_t EXE_MENU_KEYDOWN = 0x416400;
constexpr uintptr_t EXE_TEXT_CTOR = 0x41F630;           // text gadget: titles, and buttons (Enter runs +0x34)
constexpr uintptr_t EXE_BUTTON_CTOR = 0x41FD40;         // row label: passes Left, Right, Enter to its trigger
constexpr uintptr_t EXE_TRIGGER_CTOR = 0x41FC50;        // trigger gadget
constexpr uintptr_t EXE_LABEL_ITEM_CTOR = 0x41CB50;     // labelled 3D item, 0xE8 bytes
constexpr uintptr_t EXE_LABEL_ITEM_SETUP = 0x41CC20;
constexpr uintptr_t EXE_VALUE_ITEM_CTOR = 0x41E9A0;     // little trigger 3D item, 0xF0 bytes
constexpr uintptr_t EXE_VALUE_ITEM_SETUP = 0x41E9D0;

// page
constexpr size_t PG_GADGETS = 0x04, PG_PARENT = 0x10, PG_DEFAULT = 0x14;
// gadget
constexpr size_t G_NODE = 0x10, G_UP = 0x20, G_DOWN = 0x24, G_ITEM = 0x28, G_TEXT = 0x2C, G_ACTIVATED = 0x34;
constexpr size_t G_TRIGGER = 0x38;                                               // button: its trigger
constexpr size_t T_TEXTS = 0x38, T_COUNT = 0x3C, T_SELECTED = 0x40, T_CHANGED = 0x44;   // trigger
// 3D item
constexpr size_t I_NODE = 0x10, I_ON_SCREEN = 0x1C, I_START = 0x34, I_LIGHT_FOCUSED = 0x80, I_LEFT_ALIGNED = 0xDC;

using Method_t = void(__fastcall *)(void *pThis, void *pvEdx);
using AddTail_t = void(__fastcall *)(void *plh, void *pvEdx, void *pln);
using StringDuplicate_t = char *(__cdecl *)(const char *);
using FreeMemory_t = void(__cdecl *)(void *);
using ChangeToMenu_t = void(__cdecl *)(void *pgm);

Method_t _pfnOptionsInit = nullptr;
AddTail_t _pfnAddTail = nullptr;
StringDuplicate_t _pfnStringDuplicate = nullptr;
FreeMemory_t _pfnFreeMemory = nullptr;

template <typename T> T &At(void *p, size_t ofs) { return *(T *)((BYTE *)p + ofs); }
void Call(uintptr_t ulMethod, void *pThis) { ((Method_t)ulMethod)(pThis, nullptr); }

void SetGadgetText(void *pg, const char *str)
{
  char *&strText = At<char *>(pg, G_TEXT);
  char *strNew = _pfnStringDuplicate(str);
  if (strText != nullptr) _pfnFreeMemory(strText);
  strText = strNew;
}

// --- the settings ---------------------------------------------------------------

const char *const _astrOffOn[] = { "Off", "On" };
const char *const _astrAniso[] = { "Off", "2x", "4x", "8x", "16x" };
const char *const _astrAA[] = { "Off", "2x", "4x", "8x" };
const char *const _astrFov[] = { "75", "80", "85", "90", "95", "100", "105", "110", "115", "120" };
const char *const _astrWheel[] = { "Off", "Weapons" };
const char *const _astrWheelUp[] = { "Next", "Previous" };

int IndexOfPow2(int i, int iFirst)   // 1/0 -> 0, then 2 -> 1, 4 -> 2 ...
{
  int iIndex = 0;
  for (int iValue = iFirst; iValue < i; iValue *= 2) iIndex++;
  return iIndex;
}

struct Row {
  const char *strLabel;
  const char *const *astrValues;
  int ctValues;
  int (*pfnGet)();
  void (*pfnSet)(int iIndex);
};

const Row _aGraphicsRows[] = {
  { "Vertical sync", _astrOffOn, 2, [] { return g_cfg.bVSync ? 1 : 0; }, [](int i) { g_cfg.bVSync = i != 0; } },
  { "Anisotropic", _astrAniso, 5, [] { return g_cfg.iAnisotropy <= 1 ? 0 : IndexOfPow2(g_cfg.iAnisotropy, 1); },
    [](int i) { g_cfg.iAnisotropy = 1 << i; } },
  { "Anti-aliasing", _astrAA, 4, [] { return g_cfg.iAntiAliasing < 2 ? 0 : IndexOfPow2(g_cfg.iAntiAliasing, 1); },
    [](int i) { g_cfg.iAntiAliasing = i == 0 ? 0 : 1 << i; } },
  { "Field of view", _astrFov, 10, [] {
      int i = (int)floorf((g_cfg.fFov - 75.0f) / 5.0f + 0.5f);
      return i < 0 ? 0 : i > 9 ? 9 : i; },
    [](int i) { g_cfg.fFov = 75.0f + 5.0f * i; } },
  { "Fixed crosshair", _astrOffOn, 2, [] { return g_cfg.bFixedCrosshair ? 1 : 0; }, [](int i) { g_cfg.bFixedCrosshair = i != 0; } },
};
const Row _aMouseRows[] = {
  { "Raw mouse input", _astrOffOn, 2, [] { return g_cfg.bRawInput ? 1 : 0; }, [](int i) { g_cfg.bRawInput = i != 0; } },
  { "Low-latency look", _astrOffOn, 2, [] { return g_cfg.bLowLatencyLook ? 1 : 0; }, [](int i) { g_cfg.bLowLatencyLook = i != 0; } },
  { "Mouse wheel", _astrWheel, 2, [] { return g_cfg.bWheelWeapons ? 1 : 0; }, [](int i) { g_cfg.bWheelWeapons = i != 0; } },
  { "Wheel up", _astrWheelUp, 2, [] { return g_cfg.bWheelInvert ? 1 : 0; }, [](int i) { g_cfg.bWheelInvert = i != 0; } },
  { "Weapon bar", _astrOffOn, 2, [] { return g_cfg.bWeaponBar ? 1 : 0; }, [](int i) { g_cfg.bWeaponBar = i != 0; } },
};

struct Page {
  const char *strButton;        // on the hub
  const char *strTitle;         // the title bar fits about 10 letters
  const Row *aRows;
  int ctRows;
};
constexpr int MAX_ROWS = 6;   // what fits between the title and the bottom
const Page _aPages[] = {
  { "Graphics", "Graphics", _aGraphicsRows, sizeof(_aGraphicsRows) / sizeof(Row) },
  { "Mouse and weapons", "Controls", _aMouseRows, sizeof(_aMouseRows) / sizeof(Row) },
};
constexpr int CT_PAGES = sizeof(_aPages) / sizeof(Page);

// the trigger calls back with only the new index
template <int iPage, int iRow> void __cdecl RowChanged(int iIndex)
{
  if (iRow < _aPages[iPage].ctRows) _aPages[iPage].aRows[iRow].pfnSet(iIndex);
}
using Changed_t = void(__cdecl *)(int);
template <int iPage> struct PageCallbacks {
  static constexpr Changed_t apfn[MAX_ROWS] = { RowChanged<iPage, 0>, RowChanged<iPage, 1>, RowChanged<iPage, 2>,
                                                RowChanged<iPage, 3>, RowChanged<iPage, 4>, RowChanged<iPage, 5> };
};
const Changed_t *const _apfnChanged[CT_PAGES] = { PageCallbacks<0>::apfn, PageCallbacks<1>::apfn };

// --- objects, in the DLL's memory for the life of the process --------------------

// Options > AlphaFixes: a page of buttons on the main column, like the Options page
alignas(16) BYTE _aubHub[0x40];
alignas(16) BYTE _aubHubTitle[0x60];
alignas(16) BYTE _aubHubButtons[CT_PAGES][0x60];
alignas(16) BYTE _aubOptionsButton[0x60];            // on the Options page
constexpr uintptr_t _aulHubItems[CT_PAGES] = { EXE_MAIN_ITEM_1, EXE_MAIN_ITEM_2 };
// the settings pages; they share one set of 3D items for their rows
alignas(16) BYTE _aubPages[CT_PAGES][0x40];
alignas(16) BYTE _aubTitles[CT_PAGES][0x60];
alignas(16) BYTE _aubLabels[CT_PAGES][MAX_ROWS][0x60];
alignas(16) BYTE _aubValues[CT_PAGES][MAX_ROWS][0x60];
alignas(16) BYTE _aubLabelItems[MAX_ROWS][0x100];
alignas(16) BYTE _aubValueItems[MAX_ROWS][0x100];
void *_apvPageVtable[5];
int _iAntiAliasingAtStart = 0;

void SetPlacement(void *pItem, size_t ofs, float fX, float fY, float fZ)
{
  float *pf = &At<float>(pItem, ofs);
  pf[0] = fX; pf[1] = fY; pf[2] = fZ;
  pf[3] = pf[4] = pf[5] = 0.0f;
}

void *MakeItem(BYTE *pub, uintptr_t ulCtor, uintptr_t ulSetup, float fX, float fY)
{
  Call(ulCtor, pub);
  SetPlacement(pub, I_ON_SCREEN, fX, fY, -10.0f);
  SetPlacement(pub, I_START, fX == -1.5f ? 0.0f : fX, fY, -80.0f);   // fly in from straight behind, like the Video page
  Call(ulSetup, pub);
  _pfnAddTail((void *)EXE_ITEM_LIST, nullptr, pub + I_NODE);
  return pub;
}

int PageIndex(void *pgm)
{
  for (int i = 0; i < CT_PAGES; i++) {
    if (pgm == _aubPages[i]) return i;
  }
  return 0;
}

void ShowCurrentValues(int iPage)
{
  const Page &pg = _aPages[iPage];
  for (int i = 0; i < pg.ctRows; i++) {
    int iSel = pg.aRows[i].pfnGet();
    At<int>(_aubValues[iPage][i], T_SELECTED) = iSel;
    SetGadgetText(_aubValues[iPage][i], pg.aRows[i].astrValues[iSel]);
  }
}

void __fastcall Page_Start(void *pgm, void *)
{
  ShowCurrentValues(PageIndex(pgm));
  _iAntiAliasingAtStart = g_cfg.iAntiAliasing;
  Call(EXE_MENU_START, pgm);
}

void __fastcall Page_End(void *pgm, void *)
{
  Call(EXE_MENU_END, pgm);
  SaveConfig();
  if (g_cfg.iAntiAliasing != _iAntiAliasingAtStart) {
    Log("Menu: anti-aliasing changed, resetting the video mode");
    RequestVideoMode(GetCurrentVideoMode());
  }
}

void ChangeToMenu(void *pgm) { ((ChangeToMenu_t)EXE_CHANGE_TO_MENU)(pgm); }
void __cdecl OpenHub() { ChangeToMenu(_aubHub); }
template <int iPage> void __cdecl OpenPage() { ChangeToMenu(_aubPages[iPage]); }
using Activated_t = void(__cdecl *)();
const Activated_t _apfnOpenPage[CT_PAGES] = { OpenPage<0>, OpenPage<1> };

void *MakeTitle(BYTE *pub, const char *strText, void *plhGadgets)
{
  Call(EXE_TEXT_CTOR, pub);
  At<void *>(pub, G_ITEM) = (void *)EXE_TITLE_ITEM;
  SetGadgetText(pub, strText);
  _pfnAddTail(plhGadgets, nullptr, pub + G_NODE);
  return pub;
}

void BuildSettingsPage(int iPage)
{
  const Page &pg = _aPages[iPage];
  BYTE *pubPage = _aubPages[iPage];
  Call(EXE_MENU_CTOR, pubPage);
  At<void **>(pubPage, 0) = _apvPageVtable;
  At<void *>(pubPage, PG_PARENT) = _aubHub;
  void *plhGadgets = pubPage + PG_GADGETS;
  MakeTitle(_aubTitles[iPage], pg.strTitle, plhGadgets);

  for (int i = 0; i < pg.ctRows; i++) {
    BYTE *pLabel = _aubLabels[iPage][i], *pValue = _aubValues[iPage][i];
    Call(EXE_BUTTON_CTOR, pLabel);
    At<void *>(pLabel, G_ITEM) = _aubLabelItems[i];
    SetGadgetText(pLabel, pg.aRows[i].strLabel);
    At<void *>(pLabel, G_TRIGGER) = pValue;
    At<void *>(pLabel, G_UP) = _aubLabels[iPage][(i + pg.ctRows - 1) % pg.ctRows];
    At<void *>(pLabel, G_DOWN) = _aubLabels[iPage][(i + 1) % pg.ctRows];
    _pfnAddTail(plhGadgets, nullptr, pLabel + G_NODE);

    Call(EXE_TRIGGER_CTOR, pValue);
    At<void *>(pValue, G_ITEM) = _aubValueItems[i];
    At<const char *const *>(pValue, T_TEXTS) = pg.aRows[i].astrValues;
    At<int>(pValue, T_COUNT) = pg.aRows[i].ctValues;
    At<Changed_t>(pValue, T_CHANGED) = _apfnChanged[iPage][i];
    _pfnAddTail(plhGadgets, nullptr, pValue + G_NODE);
  }
  At<void *>(pubPage, PG_DEFAULT) = _aubLabels[iPage][0];
  ShowCurrentValues(iPage);
}

void BuildPages()
{
  _apvPageVtable[0] = (void *)EXE_MENU_NOTHING;   // set-up: done here
  _apvPageVtable[1] = (void *)EXE_MENU_NOTHING;
  _apvPageVtable[2] = (void *)Page_Start;
  _apvPageVtable[3] = (void *)Page_End;
  _apvPageVtable[4] = (void *)EXE_MENU_KEYDOWN;

  for (int i = 0; i < MAX_ROWS; i++) {
    float fY = 3.0f - 1.5f * i;
    void *pLabelItem = MakeItem(_aubLabelItems[i], EXE_LABEL_ITEM_CTOR, EXE_LABEL_ITEM_SETUP, -1.5f, fY);
    At<DWORD>(pLabelItem, I_LIGHT_FOCUSED) = 0xFFFFFF00;   // as the Video page's labels
    At<int>(pLabelItem, I_LEFT_ALIGNED) = 0;
    MakeItem(_aubValueItems[i], EXE_VALUE_ITEM_CTOR, EXE_VALUE_ITEM_SETUP, 5.0f, fY);
  }
  for (int i = 0; i < CT_PAGES; i++) BuildSettingsPage(i);

  // the hub: the game's plain page class
  Call(EXE_MENU_CTOR, _aubHub);
  At<void *>(_aubHub, PG_PARENT) = (void *)EXE_OPTIONS_MENU;
  void *plhGadgets = _aubHub + PG_GADGETS;
  MakeTitle(_aubHubTitle, AF_TITLE, plhGadgets);
  for (int i = 0; i < CT_PAGES; i++) {
    BYTE *pButton = _aubHubButtons[i];
    Call(EXE_TEXT_CTOR, pButton);
    At<void *>(pButton, G_ITEM) = (void *)_aulHubItems[i];
    SetGadgetText(pButton, _aPages[i].strButton);
    At<Activated_t>(pButton, G_ACTIVATED) = _apfnOpenPage[i];
    At<void *>(pButton, G_UP) = _aubHubButtons[(i + CT_PAGES - 1) % CT_PAGES];
    At<void *>(pButton, G_DOWN) = _aubHubButtons[(i + 1) % CT_PAGES];
    _pfnAddTail(plhGadgets, nullptr, pButton + G_NODE);
  }
  At<void *>(_aubHub, PG_DEFAULT) = _aubHubButtons[0];
}

// The Options page: its three buttons, then an AlphaFixes button on the next main column item.
void __fastcall OptionsInit_Hook(void *pgm, void *pvEdx)
{
  _pfnOptionsInit(pgm, pvEdx);
  BuildPages();

  Call(EXE_TEXT_CTOR, _aubOptionsButton);
  At<void *>(_aubOptionsButton, G_ITEM) = (void *)EXE_MAIN_ITEM_4;
  SetGadgetText(_aubOptionsButton, AF_TITLE);
  At<Activated_t>(_aubOptionsButton, G_ACTIVATED) = OpenHub;
  At<void *>(_aubOptionsButton, G_UP) = (void *)EXE_OPTIONS_LAST;
  At<void *>(_aubOptionsButton, G_DOWN) = (void *)EXE_OPTIONS_FIRST;
  At<void *>((void *)EXE_OPTIONS_LAST, G_DOWN) = _aubOptionsButton;
  At<void *>((void *)EXE_OPTIONS_FIRST, G_UP) = _aubOptionsButton;
  _pfnAddTail((BYTE *)pgm + PG_GADGETS, nullptr, _aubOptionsButton + G_NODE);
  Log("Menu: Options > %s pages added", AF_TITLE);
}

}  // namespace

bool InstallInGameMenu()
{
  if (!g_cfg.bInGameMenu) return true;
  _pfnAddTail = (AddTail_t)EngineProc("?AddTail@CListHead@@QAEXAAVCListNode@@@Z");
  _pfnStringDuplicate = (StringDuplicate_t)EngineProc("?StringDuplicate@@YAPADPBD@Z");
  _pfnFreeMemory = (FreeMemory_t)EngineProc("?FreeMemory@@YAXPAX@Z");
  // push ebp; mov ebp, esp; push ecx; mov eax, [0x436DE0]
  static const BYTE aubInit[] = { 0x55, 0x8B, 0xEC, 0x51, 0xA1, 0xE0, 0x6D, 0x43, 0x00 };
  if (_pfnAddTail == nullptr || _pfnStringDuplicate == nullptr || _pfnFreeMemory == nullptr
   || !CodeMatches(EXE_OPTIONS_INIT, aubInit, sizeof(aubInit))) {
    Log("Menu: SeriousSam.exe differs, no AlphaFixes page");
    return false;
  }
  return HookFunction((void *)EXE_OPTIONS_INIT, (void *)OptionsInit_Hook, (void **)&_pfnOptionsInit, "Options page set-up");
}
