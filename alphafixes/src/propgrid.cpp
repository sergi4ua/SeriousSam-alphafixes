// World Editor property grid (new).
//
// The alpha's property bar (CPropertyComboBar, dialog 105, docked right)
// shows one property at a time: a combo box picks the property, and below it
// the bar shows the one control that edits it.  The mod puts a grid over the
// bar that lists every property of the selected entities with its value, the
// way later Serious Editors do: grouped by the entity class that defines it,
// a filter box, and values edited in place.
//
// The grid is a view over the bar, not a replacement for it.  The bar keeps
// working underneath, with its controls moved out of sight:
//  - The rows are the items of the bar's property combo (IDC 1011), which the
//    editor fills on idle with the properties all selected entities share
//    (CPropertyComboBox::OnIdle in later sources).  Each item's data is a
//    CPropertyID: name (CTString at +8) and type (+0x14).
//  - Selecting a row selects that item in the combo and sends the bar
//    CBN_SELCHANGE, so the bar shows the right control for it and fills it
//    with the current value.  The editor's own features that work on the
//    selected property (picking an entity pointer target with the mouse in a
//    view, the property shortcut keys) work as before; and when they change
//    the combo's selection, the grid follows.
//  - Editing puts the new value into the bar's control for it and calls the
//    bar's UpdateData(TRUE), as the control itself does on Enter: the bar's
//    DoDataExchange (0x0041F400) writes it to every selected entity (End(),
//    set, Initialize()) and marks the document modified.  Lists (enums,
//    entity pointers, animations, illumination types) come from the bar's
//    own combo (IDC 1051) and are applied by selecting the item there.
//    Colors go through the bar's SetColorPropertyToEntities (0x004207B0),
//    file names through its browse and clear buttons.
//  - The values shown are read from the entities: each selected entity's
//    property of that name (CEntity +0x58 class, +0x18 its CDLLEntityClass,
//    whose property table and base chain PropertyForName walks).  A value the
//    selected entities don't share is shown empty.
//
// The bar is made at least MIN_BAR_WIDTH wide (its CalcDynamicLayout, vtable
// slot 0x0045C168), and a 150 ms timer keeps the grid up to date.  Keys typed
// into the grid go to it directly (a WH_GETMESSAGE hook), so the editor's
// single-key accelerators don't fire while typing a value.  Off with
// [Editor] PropertyGrid=0 in AlphaFixes.ini.
#include "alphafixes.h"

#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

/* WorldEditor.exe, build of 13 Jan 2000 (non-relocatable) */
#define WE_THEAPP               ((void *)0x004768A0)   /* CWorldEditorApp theApp */
#define WE_GETDOCUMENT          ((void *)0x0042EEF0)   /* CWorldEditorApp::GetDocument() */
#define WE_UPDATEDATA           ((void *)0x0045042C)   /* CWnd::UpdateData(BOOL) thunk (MFC42 #6334) */
#define WE_UPDATEALLVIEWS       ((void *)0x004502DC)   /* CDocument::UpdateAllViews thunk */
#define WE_SETCOLORPROPERTY     ((void *)0x004207B0)   /* CPropertyComboBar::SetColorPropertyToEntities(COLOR) */
#define WE_BAR_VTBL_CALCLAYOUT  ((void **)0x0045C168)  /* CPropertyComboBar vtable: CalcDynamicLayout */
#define WE_BAR_CALCLAYOUT       ((void *)0x00420230)   /* CPropertyComboBar::CalcDynamicLayout */
#define WE_WND_HWND             0x20                   /* CWnd::m_hWnd */
#define WE_BAR_SIZE             0x8C                   /* CPropertyComboBar::m_Size (CSize) */
#define WE_DOC_SELECTION        0x33C                  /* m_selEntitySelection: CEntity **, then the count */

/* property bar controls (dialog 105) */
#define IDC_PROPERTY_COMBO      1011
#define IDC_EDIT_ENUM           1051
#define IDC_EDIT_BOOL           1058
#define IDC_EDIT_STRING         1059
#define IDC_EDIT_FLOAT          1060
#define IDC_EDIT_COLOR          1062
#define IDC_EDIT_INDEX          1072
#define IDC_BBOX_MIN            1074
#define IDC_BBOX_MAX            1075
#define IDC_AXIS_X              1108
#define IDC_BROWSE_FILE         1015
#define IDC_NO_FILE             1016                   /* "X": no file / no target */
#define IDC_EDIT_HEADING        1032
#define IDC_ENTITY_CLASS        1111
#define IDC_ENTITY_NAME         1117
#define IDC_ENTITY_DESCRIPTION  1118

/* CPropertyID (item data of the property combo) */
#define PID_NAME                0x08                   /* CTString pid_strName */
#define PID_TYPE                0x14                   /* pid_eptType */

/* Engine.dll */
#define EN_CLASS                0x58                   /* CEntity::en_pecClass */
#define EN_VTBL_GETNAME         (0x34 / 4)             /* const CTString &CEntity::GetName() */
#define EC_DLLCLASS             0x18                   /* CEntityClass::ec_pdecDLLClass */
#define DEC_PROPERTIES          0x00                   /* CDLLEntityClass: property table, count */
#define DEC_NAME                0x18
#define DEC_BASE                0x24
#define EP_SIZE                 28                     /* CEntityProperty */
#define EP_ENUMTYPE             0x04
#define EP_OFFSET               0x0C
#define EP_NAME                 0x10
#define EN_SPAWNFLAGS           0x14                   /* CEntity::en_ulSpawnFlags */
#define EN_PARENT               0x80                   /* CEntity::en_penParent */

enum {
  EPT_ENUM = 1, EPT_BOOL = 2, EPT_FLOAT = 3, EPT_COLOR = 4, EPT_STRING = 5, EPT_RANGE = 6,
  EPT_ENTITYPTR = 7, EPT_FILENAME = 8, EPT_INDEX = 9, EPT_ANIMATION = 10, EPT_ILLUMINATIONTYPE = 11,
  EPT_FLOATAABBOX3D = 12, EPT_ANGLE = 13, EPT_FLOAT3D = 14, EPT_ANGLE3D = 15, EPT_FILENAMENODEP = 20,
  PT_PARENT = 9998, PT_SPAWNFLAGS = 9999,     /* the bar's own rows */
};

/* the bar's spawn flag checkboxes (E N H X S C D): flag, and the button's
   HWND in CPropertyComboBar (from its DoDataExchange) */
const struct { ULONG ulFlag; int iHwnd; const char *strName; } _asfSpawn[7] = {
  { 0x00001, 0x5A4, "Spawn: easy" }, { 0x00002, 0x5E8, "Spawn: normal" }, { 0x00004, 0x62C, "Spawn: hard" },
  { 0x00008, 0x670, "Spawn: extreme" }, { 0x10000, 0x6B4, "Spawn: single player" },
  { 0x40000, 0x6F8, "Spawn: cooperative" }, { 0x20000, 0x73C, "Spawn: deathmatch" },
};

#define MIN_BAR_WIDTH           300
#define MIN_BAR_HEIGHT          400                    /* floating */
#define LM_HORZDOCK             0x08                   /* MFC CalcDynamicLayout modes */
#define LM_VERTDOCK             0x10
#define TIMER_ID                1
#define TIMER_MS                150
#define WM_AF_COMMIT            (WM_APP + 1)           /* the inline editor closed: wParam = commit */

typedef void *(__fastcall *GetDocument_t)(void *pApp, void *pvEdx);
typedef BOOL  (__fastcall *UpdateData_t)(void *pWnd, void *pvEdx, BOOL bSaveAndValidate);
typedef void  (__fastcall *UpdateAllViews_t)(void *pDoc, void *pvEdx, void *pSender, LPARAM lHint, void *pHint);
typedef void  (__fastcall *SetColorProperty_t)(void *pBar, void *pvEdx, ULONG colColor);
typedef SIZE *(__fastcall *CalcDynamicLayout_t)(void *pBar, void *pvEdx, SIZE *psizeRet, int nLength, DWORD dwMode);
typedef void *(__fastcall *GetName_t)(void *pen, void *pvEdx);

typedef struct {
  void *ppid;              /* CPropertyID */
  int   iItem;             /* in the property combo */
  int   iType;
  int   iGroup;
  char  strLabel[128];     /* the combo's text: name and shortcut key */
  char  strName[128];      /* pid_strName */
  char  strValue[256];     /* shown value */
  BOOL  bMixed;            /* the selected entities differ */
  BOOL  bChecked;          /* EPT_BOOL, spawn flags */
  int   iSpawnFlag;        /* PT_SPAWNFLAGS: one row per flag, 1 + index in _asfSpawn */
  ULONG colColor;          /* EPT_COLOR */
} GridProp;

typedef struct {
  char strName[64];
  BOOL bCollapsed;
} GridGroup;

typedef struct {
  int iGroup;
  int iProp;               /* -1: the group's header row */
} GridRow;

/* values the bar's list (IDC 1051) showed for a property, to name values
   the grid can't (animations, illumination types) */
typedef struct {
  std::string strName;
  std::vector<std::pair<LONG_PTR, std::string>> aChoices;
} ChoiceCache;

CalcDynamicLayout_t _pfnCalcDynamicLayout = (CalcDynamicLayout_t)WE_BAR_CALCLAYOUT;
BOOL _bInstalled = FALSE;

void   *_pBar = NULL;                 /* CPropertyComboBar */
HWND    _hwndBar = NULL;
WNDPROC _pfnOldBarProc = NULL;
HWND    _hwndPanel = NULL;            /* the grid: header, filter and list */
HWND    _hwndFilter = NULL;
HWND    _hwndList = NULL;
HWND    _hwndEditor = NULL;           /* inline EDIT or COMBOBOX while editing */
WNDPROC _pfnOldEditorProc = NULL;
WNDPROC _pfnOldFilterProc = NULL;
HHOOK   _hhkKeys = NULL;
HFONT   _hfont = NULL, _hfontBold = NULL;
int     _cyRow = 18;
int     _cyTop = 0;                   /* list's top in the panel */
float   _fSplit = 0.45f;              /* name column / list width */
BOOL    _bSplitDrag = FALSE;

std::vector<GridProp>  _aprops;
std::vector<GridGroup> _agroups;
std::vector<GridRow>   _arows;
std::vector<ChoiceCache> _acaches;
std::vector<std::string> _astrCollapsed;   /* collapsed groups, kept across selections */
std::string _strSelected;                   /* name of the selected property, or "#group" */
int     _iTop = 0;                          /* first row shown */
int     _iEditingProp = -1;
int     _iEditMode = 0;                     /* what the inline editor commits to */
char    _strHeader[3][256];
char    _strFilter[64];
std::vector<LONG_PTR> _aulComboSignature;

enum { EM_NONE, EM_TEXT, EM_HPB, EM_BBOX, EM_CHOICE };

/* ---------------------------------------------------------------------- */
/* The editor's data                                                       */
/* ---------------------------------------------------------------------- */

void *GetDocument(void)
{
  return ((GetDocument_t)WE_GETDOCUMENT)(WE_THEAPP, NULL);
}

/* the selected entities */
char **GetSelection(int *pct)
{
  char *pDoc = (char *)GetDocument();
  *pct = 0;
  if (pDoc == NULL) return NULL;
  *pct = *(int *)(pDoc + WE_DOC_SELECTION + 4);
  return *(char ***)(pDoc + WE_DOC_SELECTION);
}

/* CDLLEntityClass of an entity */
char *EntityDLLClass(char *pen)
{
  char *pec = *(char **)(pen + EN_CLASS);
  return pec != NULL ? *(char **)(pec + EC_DLLCLASS) : NULL;
}

/* CEntityProperty of that name, and the class that defines it */
char *FindProperty(char *pen, const char *strName, char **ppdecDefiner)
{
  char *pdec;
  for (pdec = EntityDLLClass(pen); pdec != NULL; pdec = *(char **)(pdec + DEC_BASE)) {
    char *aep = *(char **)(pdec + DEC_PROPERTIES);
    int ct = *(int *)(pdec + DEC_PROPERTIES + 4), i;
    for (i = 0; i < ct; i++) {
      char *pep = aep + EP_SIZE * i;
      const char *str = *(const char **)(pep + EP_NAME);
      if (str != NULL && strcmp(str, strName) == 0) {
        if (ppdecDefiner != NULL) *ppdecDefiner = pdec;
        return pep;
      }
    }
  }
  return NULL;
}

const char *EntityName(char *pen)
{
  void **pvtbl = *(void ***)pen;
  char **pstr = (char **)((GetName_t)pvtbl[EN_VTBL_GETNAME])(pen, NULL);
  return (pstr != NULL && *pstr != NULL) ? *pstr : "";
}

HWND BarItem(int iID)
{
  return GetDlgItem(_hwndBar, iID);
}

BOOL BarItemShown(int iID)
{
  HWND hwnd = BarItem(iID);
  return hwnd != NULL && IsWindowVisible(hwnd);
}

void UpdateBarData(BOOL bSave)
{
  ((UpdateData_t)WE_UPDATEDATA)(_pBar, NULL, bSave);
}

void UpdateViews(void)
{
  void *pDoc = GetDocument();
  if (pDoc != NULL) ((UpdateAllViews_t)WE_UPDATEALLVIEWS)(pDoc, NULL, NULL, 0, NULL);
}

/* ---------------------------------------------------------------------- */
/* Values                                                                  */
/* ---------------------------------------------------------------------- */

ChoiceCache *FindCache(const char *strName)
{
  size_t i;
  for (i = 0; i < _acaches.size(); i++) {
    if (_acaches[i].strName == strName) return &_acaches[i];
  }
  return NULL;
}

/* "1.5" rather than "1.500000" */
void FormatFloat(char *str, size_t ct, float f)
{
  _snprintf(str, ct, "%g", f);
  str[ct - 1] = 0;
}

int ValueSize(int iType)
{
  switch (iType) {
  case EPT_FLOAT3D: case EPT_ANGLE3D: return 12;
  case EPT_FLOATAABBOX3D: return 24;
  default: return 4;
  }
}

BOOL IsStringType(int iType)
{
  return iType == EPT_STRING || iType == EPT_FILENAME || iType == EPT_FILENAMENODEP;
}

/* The property's value in every selected entity: the shown text, or bMixed. */
void ReadValue(GridProp *pgp)
{
  int ctSel, i;
  char **apen = GetSelection(&ctSel);
  char *pvFirst = NULL, *pepFirst = NULL;
  char *str = pgp->strValue;
  size_t ct = sizeof(pgp->strValue);

  str[0] = 0;
  pgp->bMixed = FALSE;
  pgp->bChecked = FALSE;
  pgp->colColor = 0;
  if (apen == NULL || ctSel <= 0) return;

  for (i = 0; i < ctSel; i++) {
    char *pep = FindProperty(apen[i], pgp->strName, NULL);
    char *pv;
    if (pep == NULL) {
      if (i == 0) break;         /* not a property of the class: the bar's own (parent, spawn flags) */
      continue;
    }
    pv = apen[i] + *(int *)(pep + EP_OFFSET);
    if (i == 0) {
      pvFirst = pv;
      pepFirst = pep;
    } else if (IsStringType(pgp->iType)) {
      const char *str1 = *(const char **)pvFirst, *str2 = *(const char **)pv;
      if (strcmp(str1 != NULL ? str1 : "", str2 != NULL ? str2 : "") != 0) pgp->bMixed = TRUE;
    } else if (memcmp(pvFirst, pv, ValueSize(pgp->iType)) != 0) {
      pgp->bMixed = TRUE;
    }
  }

  if (pgp->iType == PT_PARENT || pgp->iType == PT_SPAWNFLAGS) {
    for (i = 0; i < ctSel; i++) {
      ULONG ul = pgp->iType == PT_PARENT ? *(ULONG *)(apen[i] + EN_PARENT)
               : (*(ULONG *)(apen[i] + EN_SPAWNFLAGS) & _asfSpawn[pgp->iSpawnFlag - 1].ulFlag);
      if (i == 0) pgp->colColor = ul;
      else if (ul != pgp->colColor) pgp->bMixed = TRUE;
    }
    if (pgp->bMixed) return;
    if (pgp->iType == PT_SPAWNFLAGS) pgp->bChecked = pgp->colColor != 0;
    else _snprintf(str, ct - 1, "%s", pgp->colColor != 0 ? EntityName((char *)pgp->colColor) : "(none)");
    pgp->colColor = 0;
    return;
  }
  if (pvFirst == NULL) {
    /* the bar's own rows: what its control shows, while it's the selected row */
    if (_strSelected == pgp->strName && BarItemShown(IDC_EDIT_ENUM)) {
      HWND hwndCombo = BarItem(IDC_EDIT_ENUM);
      int iSel = (int)SendMessageA(hwndCombo, CB_GETCURSEL, 0, 0);
      if (iSel >= 0 && SendMessageA(hwndCombo, CB_GETLBTEXTLEN, iSel, 0) < (LRESULT)ct) {
        SendMessageA(hwndCombo, CB_GETLBTEXT, iSel, (LPARAM)str);
      }
    }
    return;
  }
  if (pgp->bMixed) return;

  switch (pgp->iType) {
  case EPT_BOOL:
    pgp->bChecked = *(int *)pvFirst != 0;
    break;
  case EPT_FLOAT: case EPT_RANGE: case EPT_ANGLE:
    FormatFloat(str, ct, *(float *)pvFirst);
    break;
  case EPT_INDEX:
    _snprintf(str, ct, "%ld", *(long *)pvFirst);
    break;
  case EPT_COLOR: {
    ULONG col = *(ULONG *)pvFirst;
    pgp->colColor = col;
    _snprintf(str, ct, "%02X %02X %02X  alpha %02X", (int)(col >> 24), (int)((col >> 16) & 0xFF),
              (int)((col >> 8) & 0xFF), (int)(col & 0xFF));
    break;
  }
  case EPT_STRING: case EPT_FILENAME: case EPT_FILENAMENODEP: {
    const char *strValue = *(const char **)pvFirst;
    if (strValue == NULL || strValue[0] == 0) {
      strValue = (pgp->iType == EPT_STRING) ? "" : "(none)";
    }
    _snprintf(str, ct, "%s", strValue);
    break;
  }
  case EPT_ENTITYPTR: {
    char *penTarget = *(char **)pvFirst;
    _snprintf(str, ct, "%s", penTarget != NULL ? EntityName(penTarget) : "(none)");
    break;
  }
  case EPT_ENUM: {
    long iValue = *(long *)pvFirst;
    char *pepet = *(char **)(pepFirst + EP_ENUMTYPE);
    _snprintf(str, ct, "%ld", iValue);
    if (pepet != NULL) {
      char *aepev = *(char **)pepet;
      int ctValues = *(int *)(pepet + 4), iValueIndex;
      for (iValueIndex = 0; iValueIndex < ctValues; iValueIndex++) {
        if (*(long *)(aepev + 8 * iValueIndex) == iValue) {
          _snprintf(str, ct, "%s", *(const char **)(aepev + 8 * iValueIndex + 4));
          break;
        }
      }
    }
    break;
  }
  case EPT_ANIMATION: case EPT_ILLUMINATIONTYPE: {
    long iValue = *(long *)pvFirst;
    ChoiceCache *pcc = FindCache(pgp->strName);
    size_t iChoice;
    _snprintf(str, ct, "%ld", iValue);
    for (iChoice = 0; pcc != NULL && iChoice < pcc->aChoices.size(); iChoice++) {
      if (pcc->aChoices[iChoice].first == iValue) {
        _snprintf(str, ct, "%s", pcc->aChoices[iChoice].second.c_str());
        break;
      }
    }
    break;
  }
  case EPT_FLOAT3D: case EPT_ANGLE3D: {
    const float *af = (const float *)pvFirst;
    char str1[32], str2[32], str3[32];
    FormatFloat(str1, sizeof(str1), af[0]);
    FormatFloat(str2, sizeof(str2), af[1]);
    FormatFloat(str3, sizeof(str3), af[2]);
    _snprintf(str, ct, "%s, %s, %s", str1, str2, str3);
    break;
  }
  case EPT_FLOATAABBOX3D: {
    const float *af = (const float *)pvFirst;
    char astr[6][32];
    for (i = 0; i < 6; i++) FormatFloat(astr[i], sizeof(astr[i]), af[i]);
    _snprintf(str, ct, "%s %s %s / %s %s %s", astr[0], astr[1], astr[2], astr[3], astr[4], astr[5]);
    break;
  }
  default:
    break;
  }
  str[ct - 1] = 0;
}

BOOL IsReadOnly(const GridProp *pgp)
{
  return pgp->iType == EPT_FLOAT3D;
}

/* ---------------------------------------------------------------------- */
/* Rows                                                                    */
/* ---------------------------------------------------------------------- */

BOOL IsCollapsed(const char *strGroup)
{
  size_t i;
  for (i = 0; i < _astrCollapsed.size(); i++) {
    if (_astrCollapsed[i] == strGroup) return TRUE;
  }
  return FALSE;
}

void SetCollapsed(const char *strGroup, BOOL bCollapsed)
{
  size_t i;
  for (i = 0; i < _astrCollapsed.size(); i++) {
    if (_astrCollapsed[i] == strGroup) {
      if (!bCollapsed) _astrCollapsed.erase(_astrCollapsed.begin() + i);
      return;
    }
  }
  if (bCollapsed) _astrCollapsed.push_back(strGroup);
}

BOOL ContainsNoCase(const char *str, const char *strPart)
{
  size_t ct = strlen(strPart), i;
  if (ct == 0) return TRUE;
  for (i = 0; str[i] != 0; i++) {
    if (_strnicmp(str + i, strPart, ct) == 0) return TRUE;
  }
  return FALSE;
}

/* rows from the properties, the filter and the collapsed groups */
void BuildRows(void)
{
  size_t iGroup, iProp;
  _arows.clear();
  for (iGroup = 0; iGroup < _agroups.size(); iGroup++) {
    std::vector<GridRow> arows;
    for (iProp = 0; iProp < _aprops.size(); iProp++) {
      if (_aprops[iProp].iGroup != (int)iGroup || !ContainsNoCase(_aprops[iProp].strLabel, _strFilter)) continue;
      GridRow gr = { (int)iGroup, (int)iProp };
      arows.push_back(gr);
    }
    if (arows.empty()) continue;
    GridRow grHeader = { (int)iGroup, -1 };
    _arows.push_back(grHeader);
    if (!_agroups[iGroup].bCollapsed) _arows.insert(_arows.end(), arows.begin(), arows.end());
  }
}

std::string PropKey(const GridProp &gp)
{
  return gp.iSpawnFlag != 0 ? std::string(gp.strLabel) : std::string(gp.strName);
}

std::string RowKey(const GridRow &gr)
{
  return gr.iProp >= 0 ? PropKey(_aprops[gr.iProp]) : "#" + std::string(_agroups[gr.iGroup].strName);
}

int SelectedRow(void)
{
  size_t i;
  for (i = 0; i < _arows.size(); i++) {
    if (RowKey(_arows[i]) == _strSelected) return (int)i;
  }
  return -1;
}

int FindGroup(const char *strName)
{
  size_t i;
  for (i = 0; i < _agroups.size(); i++) {
    if (strcmp(_agroups[i].strName, strName) == 0) return (int)i;
  }
  GridGroup gg;
  _snprintf(gg.strName, sizeof(gg.strName), "%s", strName);
  gg.strName[sizeof(gg.strName) - 1] = 0;
  gg.bCollapsed = IsCollapsed(gg.strName);
  _agroups.push_back(gg);
  return (int)_agroups.size() - 1;
}

/* the property combo's items, to see when the editor has refilled it */
std::vector<LONG_PTR> ComboSignature(void)
{
  std::vector<LONG_PTR> aul;
  HWND hwndCombo = BarItem(IDC_PROPERTY_COMBO);
  int ct = hwndCombo != NULL ? (int)SendMessageA(hwndCombo, CB_GETCOUNT, 0, 0) : 0, i;
  for (i = 0; i < ct; i++) aul.push_back((LONG_PTR)SendMessageA(hwndCombo, CB_GETITEMDATA, i, 0));
  return aul;
}

void SelectInBar(const GridProp *pgp);

void ReadProperties(void)
{
  HWND hwndCombo = BarItem(IDC_PROPERTY_COMBO);
  int ct = (int)SendMessageA(hwndCombo, CB_GETCOUNT, 0, 0), i, ctSel, iPass;
  char **apen = GetSelection(&ctSel);

  BOOL bOneClass = TRUE;
  _aprops.clear();
  _agroups.clear();
  for (i = 1; i < ctSel; i++) bOneClass &= EntityDLLClass(apen[i]) == EntityDLLClass(apen[0]);
  /* the class's properties by the class that defines them, most derived first,
     then the bar's own rows; entities of several classes share only a few */
  if (!bOneClass) FindGroup("Shared");
  for (char *pdec = (bOneClass && apen != NULL && ctSel > 0) ? EntityDLLClass(apen[0]) : NULL; pdec != NULL;
       pdec = *(char **)(pdec + DEC_BASE)) {
    const char *strClass = *(const char **)(pdec + DEC_NAME);
    FindGroup(strClass != NULL ? strClass : "?");
  }
  for (i = 0; i < ct; i++) {
    LONG_PTR ul = (LONG_PTR)SendMessageA(hwndCombo, CB_GETITEMDATA, i, 0);
    GridProp gp;
    char *pdec = NULL;
    const char *strName;
    if (ul == 0 || ul == CB_ERR) continue;   /* "None Available" */
    memset(&gp, 0, sizeof(gp));
    gp.ppid = (void *)ul;
    gp.iItem = i;
    gp.iType = *(int *)((char *)gp.ppid + PID_TYPE);
    strName = *(const char **)((char *)gp.ppid + PID_NAME);
    _snprintf(gp.strName, sizeof(gp.strName) - 1, "%s", strName != NULL ? strName : "");
    if (SendMessageA(hwndCombo, CB_GETLBTEXTLEN, i, 0) < (LRESULT)sizeof(gp.strLabel)) {
      SendMessageA(hwndCombo, CB_GETLBTEXT, i, (LPARAM)gp.strLabel);
    } else {
      strcpy(gp.strLabel, gp.strName);
    }
    if (apen != NULL && ctSel > 0 && FindProperty(apen[0], gp.strName, &pdec) != NULL) {
      const char *strClass = *(const char **)(pdec + DEC_NAME);
      gp.iGroup = FindGroup(!bOneClass ? "Shared" : strClass != NULL ? strClass : "?");
    } else {
      gp.iGroup = FindGroup("Entity");
    }
    if (gp.iType == PT_SPAWNFLAGS) {
      int iFlag;
      for (iFlag = 0; iFlag < 7; iFlag++) {
        gp.iSpawnFlag = iFlag + 1;
        _snprintf(gp.strLabel, sizeof(gp.strLabel) - 1, "%s", _asfSpawn[iFlag].strName);
        _aprops.push_back(gp);
      }
      continue;
    }
    _aprops.push_back(gp);
  }
  /* groups without properties go */
  for (iPass = (int)_agroups.size() - 1; iPass >= 0; iPass--) {
    BOOL bUsed = FALSE;
    size_t iProp;
    for (iProp = 0; iProp < _aprops.size(); iProp++) bUsed |= _aprops[iProp].iGroup == iPass;
    if (bUsed) continue;
    _agroups.erase(_agroups.begin() + iPass);
    for (iProp = 0; iProp < _aprops.size(); iProp++) {
      if (_aprops[iProp].iGroup > iPass) _aprops[iProp].iGroup--;
    }
  }
  /* names for the values of animations and illumination types, from the bar's
     list for them; then the bar's selection goes back to what it was */
  _acaches.clear();
  int iComboSel = (int)SendMessageA(hwndCombo, CB_GETCURSEL, 0, 0);
  BOOL bReselect = FALSE;
  for (size_t iProp = 0; iProp < _aprops.size(); iProp++) {
    if (_aprops[iProp].iType == EPT_ANIMATION || _aprops[iProp].iType == EPT_ILLUMINATIONTYPE) {
      SelectInBar(&_aprops[iProp]);
      bReselect = TRUE;
    }
  }
  if (bReselect && iComboSel >= 0 && (int)SendMessageA(hwndCombo, CB_GETCURSEL, 0, 0) != iComboSel) {
    SendMessageA(hwndCombo, CB_SETCURSEL, iComboSel, 0);
    SendMessageA(_hwndBar, WM_COMMAND, MAKEWPARAM(IDC_PROPERTY_COMBO, CBN_SELCHANGE), (LPARAM)hwndCombo);
  }
  for (size_t iProp = 0; iProp < _aprops.size(); iProp++) ReadValue(&_aprops[iProp]);
  _aulComboSignature = ComboSignature();
  BuildRows();
}

/* ---------------------------------------------------------------------- */
/* The bar                                                                 */
/* ---------------------------------------------------------------------- */

/* Make prop the bar's selected property, as if picked in its combo. */
void SelectInBar(const GridProp *pgp)
{
  HWND hwndCombo = BarItem(IDC_PROPERTY_COMBO);
  HWND hwndChoices;
  int ct, i;
  if ((int)SendMessageA(hwndCombo, CB_GETCURSEL, 0, 0) != pgp->iItem) {
    SendMessageA(hwndCombo, CB_SETCURSEL, pgp->iItem, 0);
    SendMessageA(_hwndBar, WM_COMMAND, MAKEWPARAM(IDC_PROPERTY_COMBO, CBN_SELCHANGE), (LPARAM)hwndCombo);
  }
  /* remember the names the bar's list gives for the values */
  if (!BarItemShown(IDC_EDIT_ENUM)) return;
  hwndChoices = BarItem(IDC_EDIT_ENUM);
  ChoiceCache *pcc = FindCache(pgp->strName);
  if (pcc == NULL) {
    ChoiceCache cc;
    cc.strName = pgp->strName;
    _acaches.push_back(cc);
    pcc = &_acaches.back();
  }
  pcc->aChoices.clear();
  ct = (int)SendMessageA(hwndChoices, CB_GETCOUNT, 0, 0);
  for (i = 0; i < ct; i++) {
    char str[256] = "";
    if (SendMessageA(hwndChoices, CB_GETLBTEXTLEN, i, 0) < (LRESULT)sizeof(str)) {
      SendMessageA(hwndChoices, CB_GETLBTEXT, i, (LPARAM)str);
    }
    pcc->aChoices.push_back(std::make_pair((LONG_PTR)SendMessageA(hwndChoices, CB_GETITEMDATA, i, 0), std::string(str)));
  }
}

/* The bar's own controls stay, out of sight: the editor still shows, hides,
   fills and reads them. */
BOOL CALLBACK HideBarControl(HWND hwnd, LPARAM lParam)
{
  RECT rc;
  (void)lParam;
  if (hwnd == _hwndPanel || GetParent(hwnd) != _hwndBar) return TRUE;
  GetWindowRect(hwnd, &rc);
  MapWindowPoints(NULL, _hwndBar, (POINT *)&rc, 2);
  if (rc.right > 0) {
    SetWindowPos(hwnd, NULL, rc.left - 10000, rc.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  return TRUE;
}

void LayoutPanel(void)
{
  RECT rc;
  if (_hwndPanel == NULL) return;
  EnumChildWindows(_hwndBar, HideBarControl, 0);
  GetClientRect(_hwndBar, &rc);
  SetWindowPos(_hwndPanel, HWND_TOP, rc.left + 2, rc.top + 2, rc.right - rc.left - 4, rc.bottom - rc.top - 4,
               SWP_NOACTIVATE);
}

/* ---------------------------------------------------------------------- */
/* Committing values                                                       */
/* ---------------------------------------------------------------------- */

BOOL ParseFloats(const char *str, float *af, int ct)
{
  int i;
  char *strEnd;
  for (i = 0; i < ct; i++) {
    while (*str == ' ' || *str == ',' || *str == '/' || *str == ';' || *str == '\t') str++;
    af[i] = (float)strtod(str, &strEnd);
    if (strEnd == str) return FALSE;
    str = strEnd;
  }
  while (*str == ' ' || *str == '\t') str++;
  return *str == 0;
}

void SetBarText(int iID, const char *str)
{
  SetWindowTextA(BarItem(iID), str);
}

void SetBarFloat(int iID, float f)
{
  char str[32];
  _snprintf(str, sizeof(str), "%.9g", f);
  str[sizeof(str) - 1] = 0;
  SetBarText(iID, str);
}

void AfterCommit(void)
{
  size_t i;
  UpdateViews();
  for (i = 0; i < _aprops.size(); i++) ReadValue(&_aprops[i]);
  InvalidateRect(_hwndList, NULL, FALSE);
}

/* text typed into the inline editor */
void CommitText(GridProp *pgp, int iMode, const char *str)
{
  float af[6];
  int i;
  SelectInBar(pgp);
  if (iMode == EM_HPB) {
    if (!ParseFloats(str, af, 3)) { MessageBeep(MB_ICONEXCLAMATION); return; }
    for (i = 0; i < 3; i++) SetBarFloat(IDC_EDIT_HEADING + i, af[i]);
    UpdateBarData(TRUE);
  } else if (iMode == EM_BBOX) {
    if (!ParseFloats(str, af, 6)) { MessageBeep(MB_ICONEXCLAMATION); return; }
    for (i = 0; i < 3; i++) {
      HWND hwndAxis = BarItem(IDC_AXIS_X + i);
      SendMessageA(hwndAxis, BM_CLICK, 0, 0);
      SetBarFloat(IDC_BBOX_MIN, af[i]);
      SetBarFloat(IDC_BBOX_MAX, af[3 + i]);
      UpdateBarData(TRUE);
    }
  } else {
    int iID = BarItemShown(IDC_EDIT_FLOAT) ? IDC_EDIT_FLOAT : BarItemShown(IDC_EDIT_INDEX) ? IDC_EDIT_INDEX : IDC_EDIT_STRING;
    if (iID != IDC_EDIT_STRING) {
      char *strEnd;
      if (iID == IDC_EDIT_INDEX) strtol(str, &strEnd, 10); else strtod(str, &strEnd);
      while (*strEnd == ' ') strEnd++;
      if (strEnd == str || *strEnd != 0) { MessageBeep(MB_ICONEXCLAMATION); return; }
    }
    SetBarText(iID, str);
    UpdateBarData(TRUE);
  }
  AfterCommit();
}

void CommitChoice(GridProp *pgp, int iChoice)
{
  HWND hwndChoices = BarItem(IDC_EDIT_ENUM);
  SelectInBar(pgp);
  SendMessageA(hwndChoices, CB_SETCURSEL, iChoice, 0);
  SendMessageA(_hwndBar, WM_COMMAND, MAKEWPARAM(IDC_EDIT_ENUM, CBN_SELCHANGE), (LPARAM)hwndChoices);
  AfterCommit();
}

void ToggleBool(GridProp *pgp)
{
  HWND hwndCheck = BarItem(IDC_EDIT_BOOL);
  SelectInBar(pgp);
  if (!BarItemShown(IDC_EDIT_BOOL)) return;
  SendMessageA(hwndCheck, BM_SETCHECK, (pgp->bChecked && !pgp->bMixed) ? BST_UNCHECKED : BST_CHECKED, 0);
  UpdateBarData(TRUE);
  AfterCommit();
}

void ToggleSpawnFlag(GridProp *pgp)
{
  HWND hwndCheck;
  SelectInBar(pgp);
  hwndCheck = *(HWND *)((char *)_pBar + _asfSpawn[pgp->iSpawnFlag - 1].iHwnd);
  if (!IsWindow(hwndCheck) || !IsWindowVisible(hwndCheck)) return;
  SendMessageA(hwndCheck, BM_SETCHECK, (pgp->bChecked && !pgp->bMixed) ? BST_UNCHECKED : BST_CHECKED, 0);
  UpdateBarData(TRUE);
  AfterCommit();
}

void ChooseColorValue(GridProp *pgp)
{
  static COLORREF acrCustom[16];
  CHOOSECOLORA cc;
  ULONG col = pgp->colColor;
  SelectInBar(pgp);
  memset(&cc, 0, sizeof(cc));
  cc.lStructSize = sizeof(cc);
  cc.hwndOwner = GetAncestor(_hwndBar, GA_ROOT);
  cc.rgbResult = RGB((col >> 24) & 0xFF, (col >> 16) & 0xFF, (col >> 8) & 0xFF);
  cc.lpCustColors = acrCustom;
  cc.Flags = CC_RGBINIT | CC_FULLOPEN;
  if (!ChooseColorA(&cc)) return;
  col = ((ULONG)GetRValue(cc.rgbResult) << 24) | ((ULONG)GetGValue(cc.rgbResult) << 16)
      | ((ULONG)GetBValue(cc.rgbResult) << 8) | (pgp->bMixed ? 0xFF : (pgp->colColor & 0xFF));
  ((SetColorProperty_t)WE_SETCOLORPROPERTY)(_pBar, NULL, col);
  AfterCommit();
}

void ClickBarButton(GridProp *pgp, int iID)
{
  SelectInBar(pgp);
  if (!BarItemShown(iID)) return;
  EnableWindow(BarItem(iID), TRUE);
  SendMessageA(BarItem(iID), BM_CLICK, 0, 0);
  AfterCommit();
}

/* ---------------------------------------------------------------------- */
/* Inline editor                                                           */
/* ---------------------------------------------------------------------- */

void ValueRect(int iRow, RECT *prc)
{
  RECT rcList;
  GetClientRect(_hwndList, &rcList);
  prc->left = (int)((rcList.right - rcList.left) * _fSplit) + 1;
  prc->right = rcList.right;
  prc->top = (iRow - _iTop) * _cyRow;
  prc->bottom = prc->top + _cyRow;
}

void EndEdit(BOOL bCommit)
{
  HWND hwnd = _hwndEditor;
  int iProp = _iEditingProp, iMode = _iEditMode;
  char str[512] = "";
  int iChoice = -1;
  if (hwnd == NULL) return;
  _hwndEditor = NULL;
  _iEditingProp = -1;
  if (iMode == EM_CHOICE) iChoice = (int)SendMessageA(hwnd, CB_GETCURSEL, 0, 0);
  else GetWindowTextA(hwnd, str, sizeof(str));
  if (GetFocus() == hwnd) SetFocus(_hwndList);
  DestroyWindow(hwnd);
  if (!bCommit || iProp < 0 || iProp >= (int)_aprops.size()) return;
  if (iMode == EM_CHOICE) {
    if (iChoice >= 0) CommitChoice(&_aprops[iProp], iChoice);
  } else {
    CommitText(&_aprops[iProp], iMode, str);
  }
}

LRESULT CALLBACK EditorProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  WNDPROC pfnOld = _pfnOldEditorProc;
  switch (uMsg) {
  case WM_GETDLGCODE:
    return DLGC_WANTALLKEYS | CallWindowProcA(pfnOld, hwnd, uMsg, wParam, lParam);
  case WM_KEYDOWN:
    if (wParam == VK_RETURN) { PostMessageA(_hwndList, WM_AF_COMMIT, TRUE, 0); return 0; }
    if (wParam == VK_ESCAPE) { PostMessageA(_hwndList, WM_AF_COMMIT, FALSE, 0); return 0; }
    break;
  case WM_CHAR:
    if (wParam == '\r' || wParam == 27) return 0;
    break;
  case WM_KILLFOCUS:
    /* a combo box's own list takes the focus while it's open */
    if (hwnd == _hwndEditor && !(GetAncestor((HWND)wParam, GA_PARENT) == hwnd)) {
      PostMessageA(_hwndList, WM_AF_COMMIT, _iEditMode != EM_CHOICE, 0);
    }
    break;
  }
  return CallWindowProcA(pfnOld, hwnd, uMsg, wParam, lParam);
}

void BeginEdit(int iRow)
{
  GridProp *pgp;
  RECT rc;
  int iProp;
  if (iRow < 0 || iRow >= (int)_arows.size() || _arows[iRow].iProp < 0) return;
  EndEdit(TRUE);
  iProp = _arows[iRow].iProp;
  pgp = &_aprops[iProp];
  if (IsReadOnly(pgp)) return;
  SelectInBar(pgp);
  ValueRect(iRow, &rc);

  if (pgp->iSpawnFlag != 0) {
    ToggleSpawnFlag(pgp);
    return;
  }
  if (pgp->iType == EPT_BOOL || BarItemShown(IDC_EDIT_BOOL)) {
    ToggleBool(pgp);
    return;
  }
  if (pgp->iType == EPT_COLOR || BarItemShown(IDC_EDIT_COLOR)) {
    ChooseColorValue(pgp);
    return;
  }
  if (BarItemShown(IDC_EDIT_ENUM)) {
    HWND hwndChoices = BarItem(IDC_EDIT_ENUM);
    int ct = (int)SendMessageA(hwndChoices, CB_GETCOUNT, 0, 0), i;
    _hwndEditor = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                                  rc.left, rc.top - 1, rc.right - rc.left, 300, _hwndList, NULL, NULL, NULL);
    SendMessageA(_hwndEditor, WM_SETFONT, (WPARAM)_hfont, FALSE);
    for (i = 0; i < ct; i++) {
      char str[256] = "";
      if (SendMessageA(hwndChoices, CB_GETLBTEXTLEN, i, 0) < (LRESULT)sizeof(str)) {
        SendMessageA(hwndChoices, CB_GETLBTEXT, i, (LPARAM)str);
      }
      SendMessageA(_hwndEditor, CB_ADDSTRING, 0, (LPARAM)str);
    }
    SendMessageA(_hwndEditor, CB_SETCURSEL, SendMessageA(hwndChoices, CB_GETCURSEL, 0, 0), 0);
    _iEditMode = EM_CHOICE;
  } else if (pgp->iType == EPT_FILENAME || pgp->iType == EPT_FILENAMENODEP || BarItemShown(IDC_BROWSE_FILE)) {
    ClickBarButton(pgp, IDC_BROWSE_FILE);
    return;
  } else {
    char str[256] = "";
    if (BarItemShown(IDC_EDIT_HEADING)) {
      _iEditMode = EM_HPB;
      strcpy(str, pgp->strValue);
    } else if (BarItemShown(IDC_BBOX_MIN)) {
      _iEditMode = EM_BBOX;
      strcpy(str, pgp->strValue);
    } else if (BarItemShown(IDC_EDIT_FLOAT) || BarItemShown(IDC_EDIT_INDEX) || BarItemShown(IDC_EDIT_STRING)) {
      int iID = BarItemShown(IDC_EDIT_FLOAT) ? IDC_EDIT_FLOAT : BarItemShown(IDC_EDIT_INDEX) ? IDC_EDIT_INDEX : IDC_EDIT_STRING;
      _iEditMode = EM_TEXT;
      if (pgp->bMixed) str[0] = 0;
      else if (iID == IDC_EDIT_STRING) strcpy(str, pgp->strValue);
      else GetWindowTextA(BarItem(iID), str, sizeof(str));
    } else {
      return;                    /* nothing in the bar edits it */
    }
    _hwndEditor = CreateWindowExA(0, "EDIT", str, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                  rc.left + 2, rc.top + 1, rc.right - rc.left - 3, rc.bottom - rc.top - 2,
                                  _hwndList, NULL, NULL, NULL);
    SendMessageA(_hwndEditor, WM_SETFONT, (WPARAM)_hfont, FALSE);
    SendMessageA(_hwndEditor, EM_SETSEL, 0, -1);
  }
  _iEditingProp = iProp;
  _pfnOldEditorProc = (WNDPROC)SetWindowLongPtrA(_hwndEditor, GWLP_WNDPROC, (LONG_PTR)EditorProc);
  SetFocus(_hwndEditor);
  if (_iEditMode == EM_CHOICE) SendMessageA(_hwndEditor, CB_SHOWDROPDOWN, TRUE, 0);
}

/* ---------------------------------------------------------------------- */
/* The list                                                                */
/* ---------------------------------------------------------------------- */

int VisibleRows(void)
{
  RECT rc;
  GetClientRect(_hwndList, &rc);
  return (rc.bottom - rc.top) / _cyRow;
}

void UpdateScrollBar(void)
{
  SCROLLINFO si;
  int ctMax = (int)_arows.size() - VisibleRows();
  if (_iTop > ctMax) _iTop = ctMax;
  if (_iTop < 0) _iTop = 0;
  memset(&si, 0, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
  si.nMin = 0;
  si.nMax = (int)_arows.size() - 1;
  si.nPage = VisibleRows();
  si.nPos = _iTop;
  SetScrollInfo(_hwndList, SB_VERT, &si, TRUE);
}

void EnsureVisible(int iRow)
{
  int ctVisible = VisibleRows();
  if (iRow < 0) return;
  if (iRow < _iTop) _iTop = iRow;
  else if (ctVisible > 0 && iRow >= _iTop + ctVisible) _iTop = iRow - ctVisible + 1;
  UpdateScrollBar();
}

void SelectRow(int iRow, BOOL bInBar)
{
  if (iRow < 0 || iRow >= (int)_arows.size()) return;
  EndEdit(TRUE);
  _strSelected = RowKey(_arows[iRow]);
  if (bInBar && _arows[iRow].iProp >= 0) SelectInBar(&_aprops[_arows[iRow].iProp]);
  EnsureVisible(iRow);
  InvalidateRect(_hwndList, NULL, FALSE);
}

void ToggleGroup(int iRow)
{
  GridGroup *pgg = &_agroups[_arows[iRow].iGroup];
  EndEdit(TRUE);
  pgg->bCollapsed = !pgg->bCollapsed;
  SetCollapsed(pgg->strName, pgg->bCollapsed);
  _strSelected = RowKey(_arows[iRow]);
  BuildRows();
  UpdateScrollBar();
  InvalidateRect(_hwndList, NULL, FALSE);
}

void DrawButton(HDC hdc, RECT *prc, const char *str)
{
  DrawFrameControl(hdc, prc, DFC_BUTTON, DFCS_BUTTONPUSH);
  DrawTextA(hdc, str, -1, prc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* the buttons drawn at the right of the selected row's value */
int RowButtons(const GridProp *pgp, BOOL *pbBrowse, BOOL *pbClear, BOOL *pbDrop)
{
  *pbBrowse = pgp->iType == EPT_FILENAME || pgp->iType == EPT_FILENAMENODEP;
  *pbClear = (*pbBrowse || pgp->iType == EPT_ENTITYPTR) && BarItemShown(IDC_NO_FILE);
  *pbDrop = !*pbBrowse && pgp->iType != EPT_BOOL && pgp->iType != EPT_COLOR && pgp->iSpawnFlag == 0
         && BarItemShown(IDC_EDIT_ENUM);
  return (*pbBrowse ? 1 : 0) + (*pbClear ? 1 : 0) + (*pbDrop ? 1 : 0);
}

void PaintList(HDC hdcWnd)
{
  RECT rcClient, rc;
  HDC hdc;
  HBITMAP hbm, hbmOld;
  HFONT hfontOld;
  int cxSplit, iRow, iSel = SelectedRow();
  BOOL bFocus = GetFocus() == _hwndList || (_hwndEditor != NULL && GetFocus() == _hwndEditor);
  HPEN hpenGrid = CreatePen(PS_SOLID, 1, RGB(222, 222, 222)), hpenOld;
  HBRUSH hbrGroup = CreateSolidBrush(RGB(250, 246, 222));
  HBRUSH hbrSel = bFocus ? GetSysColorBrush(COLOR_HIGHLIGHT) : CreateSolidBrush(RGB(214, 214, 214));

  GetClientRect(_hwndList, &rcClient);
  hdc = CreateCompatibleDC(hdcWnd);
  hbm = CreateCompatibleBitmap(hdcWnd, rcClient.right, rcClient.bottom);
  hbmOld = (HBITMAP)SelectObject(hdc, hbm);
  hfontOld = (HFONT)SelectObject(hdc, _hfont);
  hpenOld = (HPEN)SelectObject(hdc, hpenGrid);
  FillRect(hdc, &rcClient, GetSysColorBrush(COLOR_WINDOW));
  SetBkMode(hdc, TRANSPARENT);
  cxSplit = (int)(rcClient.right * _fSplit);

  if (_aprops.empty()) {
    rc = rcClient;
    rc.top += 8;
    SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
    DrawTextA(hdc, "Select entities to see their properties.", -1, &rc, DT_CENTER | DT_WORDBREAK);
  }

  for (iRow = _iTop; iRow < (int)_arows.size(); iRow++) {
    const GridRow &gr = _arows[iRow];
    int y = (iRow - _iTop) * _cyRow;
    if (y > rcClient.bottom) break;
    rc.left = 0; rc.right = rcClient.right; rc.top = y; rc.bottom = y + _cyRow;

    if (gr.iProp < 0) {
      /* group header: [-] Name */
      const GridGroup &gg = _agroups[gr.iGroup];
      RECT rcBox = { 4, y + (_cyRow - 9) / 2, 13, y + (_cyRow - 9) / 2 + 9 };
      FillRect(hdc, &rc, iRow == iSel ? hbrSel : hbrGroup);
      FrameRect(hdc, &rcBox, GetSysColorBrush(COLOR_GRAYTEXT));
      MoveToEx(hdc, rcBox.left + 2, rcBox.top + 4, NULL);
      LineTo(hdc, rcBox.right - 2, rcBox.top + 4);
      if (gg.bCollapsed) {
        MoveToEx(hdc, rcBox.left + 4, rcBox.top + 2, NULL);
        LineTo(hdc, rcBox.left + 4, rcBox.bottom - 2);
      }
      SelectObject(hdc, _hfontBold);
      SetTextColor(hdc, GetSysColor(iRow == iSel && bFocus ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
      rc.left = 18;
      DrawTextA(hdc, gg.strName, -1, &rc, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
      SelectObject(hdc, _hfont);
    } else {
      const GridProp &gp = _aprops[gr.iProp];
      RECT rcName = { 0, y, cxSplit, y + _cyRow }, rcValue = { cxSplit + 1, y, rcClient.right, y + _cyRow };
      BOOL bSel = iRow == iSel;
      /* name */
      if (bSel) FillRect(hdc, &rcName, hbrSel);
      SetTextColor(hdc, GetSysColor(bSel && bFocus ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
      rcName.left = 18;
      DrawTextA(hdc, gp.strLabel, -1, &rcName, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
      /* value */
      SetTextColor(hdc, GetSysColor(IsReadOnly(&gp) ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
      rcValue.left += 4;
      if ((gp.iType == EPT_BOOL && gp.strValue[0] == 0) || gp.iSpawnFlag != 0) {
        RECT rcCheck = { rcValue.left, y + (_cyRow - 13) / 2, rcValue.left + 13, y + (_cyRow - 13) / 2 + 13 };
        UINT uState = DFCS_FLAT | (gp.bMixed ? DFCS_BUTTON3STATE | DFCS_CHECKED : DFCS_BUTTONCHECK)
                    | (gp.bChecked ? DFCS_CHECKED : 0);
        DrawFrameControl(hdc, &rcCheck, DFC_BUTTON, uState);
      } else if (gp.iType == EPT_COLOR && !gp.bMixed) {
        RECT rcSwatch = { rcValue.left, y + 3, rcValue.left + 24, y + _cyRow - 3 };
        HBRUSH hbr = CreateSolidBrush(RGB((gp.colColor >> 24) & 0xFF, (gp.colColor >> 16) & 0xFF, (gp.colColor >> 8) & 0xFF));
        FillRect(hdc, &rcSwatch, hbr);
        FrameRect(hdc, &rcSwatch, GetSysColorBrush(COLOR_WINDOWTEXT));
        DeleteObject(hbr);
        rcValue.left += 30;
        DrawTextA(hdc, gp.strValue, -1, &rcValue, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
      } else {
        if (bSel) {
          BOOL bBrowse, bClear, bDrop;
          int ctButtons = RowButtons(&gp, &bBrowse, &bClear, &bDrop);
          RECT rcButton = { rcClient.right - _cyRow, y + 1, rcClient.right - 1, y + _cyRow - 1 };
          if (bDrop) {
            DrawFrameControl(hdc, &rcButton, DFC_SCROLL, DFCS_SCROLLCOMBOBOX);
            OffsetRect(&rcButton, -_cyRow, 0);
          }
          if (bClear) {
            DrawButton(hdc, &rcButton, "x");
            OffsetRect(&rcButton, -_cyRow, 0);
          }
          if (bBrowse) DrawButton(hdc, &rcButton, "...");
          rcValue.right -= ctButtons * _cyRow;
        }
        DrawTextA(hdc, gp.strValue, -1, &rcValue, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
      }
      /* grid lines */
      MoveToEx(hdc, cxSplit, y, NULL);
      LineTo(hdc, cxSplit, y + _cyRow);
    }
    MoveToEx(hdc, 0, y + _cyRow - 1, NULL);
    LineTo(hdc, rcClient.right, y + _cyRow - 1);
  }

  BitBlt(hdcWnd, 0, 0, rcClient.right, rcClient.bottom, hdc, 0, 0, SRCCOPY);
  SelectObject(hdc, hpenOld);
  SelectObject(hdc, hfontOld);
  SelectObject(hdc, hbmOld);
  DeleteObject(hbm);
  DeleteDC(hdc);
  DeleteObject(hpenGrid);
  DeleteObject(hbrGroup);
  if (!bFocus) DeleteObject(hbrSel);
}

/* a click in the value of a row */
void ClickValue(int iRow, int x)
{
  RECT rcClient;
  GridProp *pgp = &_aprops[_arows[iRow].iProp];
  BOOL bBrowse, bClear, bDrop;
  int iButton;
  GetClientRect(_hwndList, &rcClient);
  SelectInBar(pgp);
  RowButtons(pgp, &bBrowse, &bClear, &bDrop);
  iButton = (rcClient.right - x) / _cyRow;    /* 0 = rightmost */
  if (bDrop && iButton == 0) { BeginEdit(iRow); return; }
  if (bDrop) iButton--;
  if (bClear && iButton == 0) { ClickBarButton(pgp, IDC_NO_FILE); return; }
  if (bClear) iButton--;
  if (bBrowse && iButton == 0) { ClickBarButton(pgp, IDC_BROWSE_FILE); return; }
  /* elsewhere: edit, but a file name only with its button */
  if (!bBrowse) BeginEdit(iRow);
}

int RowAt(int y)
{
  int iRow = _iTop + y / _cyRow;
  return (y >= 0 && iRow < (int)_arows.size()) ? iRow : -1;
}

void Scroll(int iNewTop)
{
  EndEdit(TRUE);
  _iTop = iNewTop;
  UpdateScrollBar();
  InvalidateRect(_hwndList, NULL, FALSE);
}

LRESULT CALLBACK ListProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    PaintList(hdc);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_ERASEBKGND:
    return 1;
  case WM_SIZE:
    EndEdit(TRUE);
    UpdateScrollBar();
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_GETDLGCODE:
    return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS;
  case WM_SETFOCUS: case WM_KILLFOCUS:
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_SETCURSOR: {
    POINT pt;
    RECT rc;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    GetClientRect(hwnd, &rc);
    if (LOWORD(lParam) == HTCLIENT && abs(pt.x - (int)(rc.right * _fSplit)) <= 3) {
      SetCursor(LoadCursor(NULL, IDC_SIZEWE));
      return TRUE;
    }
    break;
  }
  case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
    int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam), iRow = RowAt(y);
    RECT rc;
    BOOL bWasSelected = iRow >= 0 && iRow == SelectedRow();
    GetClientRect(hwnd, &rc);
    SetFocus(hwnd);
    if (abs(x - (int)(rc.right * _fSplit)) <= 3) {
      _bSplitDrag = TRUE;
      SetCapture(hwnd);
      return 0;
    }
    if (iRow < 0) return 0;
    if (_arows[iRow].iProp < 0) {
      if (x < 18 || uMsg == WM_LBUTTONDBLCLK) ToggleGroup(iRow); else SelectRow(iRow, TRUE);
      return 0;
    }
    SelectRow(iRow, TRUE);
    if (x > (int)(rc.right * _fSplit)) {
      /* a checkbox flips with the first click; anything else edits on a click of the selected row */
      const GridProp &gp = _aprops[_arows[iRow].iProp];
      if (bWasSelected || gp.iType == EPT_BOOL || gp.iSpawnFlag != 0 || uMsg == WM_LBUTTONDBLCLK) ClickValue(iRow, x);
    } else if (uMsg == WM_LBUTTONDBLCLK) {
      BeginEdit(iRow);
    }
    return 0;
  }
  case WM_MOUSEMOVE:
    if (_bSplitDrag) {
      RECT rc;
      GetClientRect(hwnd, &rc);
      if (rc.right > 0) {
        _fSplit = (float)(short)LOWORD(lParam) / rc.right;
        if (_fSplit < 0.15f) _fSplit = 0.15f;
        if (_fSplit > 0.85f) _fSplit = 0.85f;
        InvalidateRect(hwnd, NULL, FALSE);
      }
    }
    return 0;
  case WM_LBUTTONUP:
    if (_bSplitDrag) {
      _bSplitDrag = FALSE;
      ReleaseCapture();
    }
    return 0;
  case WM_MOUSEWHEEL:
    Scroll(_iTop - (short)HIWORD(wParam) / WHEEL_DELTA * 3);
    return 0;
  case WM_VSCROLL: {
    SCROLLINFO si;
    int iTop = _iTop;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_TRACKPOS;
    GetScrollInfo(hwnd, SB_VERT, &si);
    switch (LOWORD(wParam)) {
    case SB_LINEUP: iTop--; break;
    case SB_LINEDOWN: iTop++; break;
    case SB_PAGEUP: iTop -= VisibleRows(); break;
    case SB_PAGEDOWN: iTop += VisibleRows(); break;
    case SB_THUMBTRACK: case SB_THUMBPOSITION: iTop = si.nTrackPos; break;
    case SB_TOP: iTop = 0; break;
    case SB_BOTTOM: iTop = (int)_arows.size(); break;
    }
    Scroll(iTop);
    return 0;
  }
  case WM_KEYDOWN: {
    int iSel = SelectedRow(), iNew = iSel;
    switch (wParam) {
    case VK_UP: iNew = iSel - 1; break;
    case VK_DOWN: iNew = iSel + 1; break;
    case VK_PRIOR: iNew = iSel - VisibleRows(); break;
    case VK_NEXT: iNew = iSel + VisibleRows(); break;
    case VK_HOME: iNew = 0; break;
    case VK_END: iNew = (int)_arows.size() - 1; break;
    case VK_LEFT: case VK_RIGHT:
      if (iSel >= 0 && _arows[iSel].iProp < 0 && _agroups[_arows[iSel].iGroup].bCollapsed == (wParam == VK_RIGHT)) {
        ToggleGroup(iSel);
      }
      return 0;
    case VK_RETURN: case VK_F2: case VK_SPACE:
      if (iSel >= 0 && _arows[iSel].iProp < 0) ToggleGroup(iSel);
      else if (iSel >= 0) {
        GridProp *pgp = &_aprops[_arows[iSel].iProp];
        if (pgp->iType == EPT_FILENAME || pgp->iType == EPT_FILENAMENODEP) ClickBarButton(pgp, IDC_BROWSE_FILE);
        else BeginEdit(iSel);
      }
      return 0;
    default:
      return 0;
    }
    if (iSel < 0) iNew = 0;
    if (iNew < 0) iNew = 0;
    if (iNew >= (int)_arows.size()) iNew = (int)_arows.size() - 1;
    SelectRow(iNew, TRUE);
    return 0;
  }
  case WM_AF_COMMIT:
    EndEdit((BOOL)wParam);
    return 0;
  case WM_COMMAND:
    if ((HWND)lParam == _hwndEditor && _iEditMode == EM_CHOICE) {
      if (HIWORD(wParam) == CBN_SELENDOK) PostMessageA(hwnd, WM_AF_COMMIT, TRUE, 0);
      else if (HIWORD(wParam) == CBN_SELENDCANCEL) PostMessageA(hwnd, WM_AF_COMMIT, FALSE, 0);
    }
    return 0;
  }
  return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

/* ---------------------------------------------------------------------- */
/* The panel: entity header, filter, list                                  */
/* ---------------------------------------------------------------------- */

void ReadHeader(char astr[3][256])
{
  static const int aiID[3] = { IDC_ENTITY_CLASS, IDC_ENTITY_NAME, IDC_ENTITY_DESCRIPTION };
  int i;
  for (i = 0; i < 3; i++) {
    astr[i][0] = 0;
    GetWindowTextA(BarItem(aiID[i]), astr[i], 256);
  }
}

void LayoutPanelChildren(void)
{
  RECT rc;
  int cyFilter = _cyRow + 4, yFilter = 2 * _cyRow + 8;
  GetClientRect(_hwndPanel, &rc);
  SetWindowPos(_hwndFilter, NULL, 40, yFilter, rc.right - 42, cyFilter, SWP_NOZORDER | SWP_NOACTIVATE);
  _cyTop = yFilter + cyFilter + 4;
  SetWindowPos(_hwndList, NULL, 0, _cyTop, rc.right, rc.bottom - _cyTop > 0 ? rc.bottom - _cyTop : 0,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

void PaintPanel(HDC hdc)
{
  RECT rc, rcText;
  HFONT hfontOld = (HFONT)SelectObject(hdc, _hfontBold);
  char str[600];
  GetClientRect(_hwndPanel, &rc);
  FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
  rcText.left = 4; rcText.right = rc.right - 4; rcText.top = 3; rcText.bottom = rcText.top + _cyRow;
  DrawTextA(hdc, _strHeader[0], -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
  SelectObject(hdc, _hfont);
  OffsetRect(&rcText, 0, _cyRow);
  if (_strHeader[2][0] != 0 && strcmp(_strHeader[2], "No description") != 0) {
    _snprintf(str, sizeof(str), "%s  -  %s", _strHeader[1], _strHeader[2]);
  } else {
    _snprintf(str, sizeof(str), "%s", _strHeader[1]);
  }
  str[sizeof(str) - 1] = 0;
  DrawTextA(hdc, str, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
  rcText.top = 2 * _cyRow + 8; rcText.bottom = rcText.top + _cyRow + 4; rcText.left = 4; rcText.right = 38;
  DrawTextA(hdc, "Filter:", -1, &rcText, DT_SINGLELINE | DT_VCENTER);
  SelectObject(hdc, hfontOld);
}

/* everything the timer keeps up to date */
void Refresh(void)
{
  char astrHeader[3][256];
  size_t i;
  BOOL bChanged = FALSE;
  HWND hwndCombo;
  int iComboSel;

  if (_hwndPanel == NULL || !IsWindowVisible(_hwndPanel)) return;
  EnumChildWindows(_hwndBar, HideBarControl, 0);

  ReadHeader(astrHeader);
  if (memcmp(astrHeader, _strHeader, sizeof(astrHeader)) != 0) {
    memcpy(_strHeader, astrHeader, sizeof(astrHeader));
    InvalidateRect(_hwndPanel, NULL, FALSE);
  }

  if (_hwndEditor != NULL) return;     /* the rows stay while a value is edited */

  if (ComboSignature() != _aulComboSignature) {
    ReadProperties();
    UpdateScrollBar();
    bChanged = TRUE;
  } else {
    for (i = 0; i < _aprops.size(); i++) {
      GridProp gp = _aprops[i];
      ReadValue(&_aprops[i]);
      if (strcmp(gp.strValue, _aprops[i].strValue) != 0 || gp.bMixed != _aprops[i].bMixed
       || gp.bChecked != _aprops[i].bChecked) {
        bChanged = TRUE;
      }
    }
  }

  /* the bar's selected property, when the editor changed it (shortcut keys) */
  hwndCombo = BarItem(IDC_PROPERTY_COMBO);
  iComboSel = (int)SendMessageA(hwndCombo, CB_GETCURSEL, 0, 0);
  int iSelRow = SelectedRow();
  BOOL bSynced = iSelRow < 0 ? _strSelected[0] == '#'
               : _arows[iSelRow].iProp < 0 || _aprops[_arows[iSelRow].iProp].iItem == iComboSel;
  for (i = 0; !bSynced && i < _aprops.size(); i++) {
    if (_aprops[i].iItem == iComboSel) {
      bSynced = TRUE;
      _strSelected = PropKey(_aprops[i]);
      if (SelectedRow() < 0 && _agroups[_aprops[i].iGroup].bCollapsed) {
        _agroups[_aprops[i].iGroup].bCollapsed = FALSE;
        SetCollapsed(_agroups[_aprops[i].iGroup].strName, FALSE);
        BuildRows();
      }
      EnsureVisible(SelectedRow());
      bChanged = TRUE;
    }
  }
  if (bChanged) InvalidateRect(_hwndList, NULL, FALSE);
}

LRESULT CALLBACK FilterProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_GETDLGCODE) return DLGC_WANTALLKEYS | CallWindowProcA(_pfnOldFilterProc, hwnd, uMsg, wParam, lParam);
  if (uMsg == WM_KEYDOWN && wParam == VK_ESCAPE) {
    SetWindowTextA(hwnd, "");
    return 0;
  }
  if (uMsg == WM_KEYDOWN && (wParam == VK_DOWN || wParam == VK_RETURN)) {
    SetFocus(_hwndList);
    if (SelectedRow() < 0 && !_arows.empty()) SelectRow(0, TRUE);
    return 0;
  }
  if (uMsg == WM_CHAR && (wParam == '\r' || wParam == 27)) return 0;
  return CallWindowProcA(_pfnOldFilterProc, hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK PanelProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    PaintPanel(hdc);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_ERASEBKGND:
    return 1;
  case WM_SIZE:
    LayoutPanelChildren();
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_TIMER:
    if (wParam == TIMER_ID) Refresh();
    return 0;
  case WM_COMMAND:
    if ((HWND)lParam == _hwndFilter && HIWORD(wParam) == EN_CHANGE) {
      GetWindowTextA(_hwndFilter, _strFilter, sizeof(_strFilter));
      EndEdit(TRUE);
      BuildRows();
      _iTop = 0;
      UpdateScrollBar();
      InvalidateRect(_hwndList, NULL, FALSE);
    }
    return 0;
  case WM_CTLCOLOREDIT:
    return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
  }
  return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

/* ---------------------------------------------------------------------- */
/* Keys                                                                    */
/* ---------------------------------------------------------------------- */

/* Keys for the grid go straight to it.  Otherwise the editor's frame would
   translate them as accelerators (letters are editor commands) and the dialog
   bar would use them for dialog navigation.  Ctrl and Alt keys still reach
   the editor (Ctrl+S, Ctrl+Z), except the text editing ones in an edit box. */
LRESULT CALLBACK KeyHook(int nCode, WPARAM wParam, LPARAM lParam)
{
  MSG *pmsg = (MSG *)lParam;
  if (nCode == HC_ACTION && wParam == PM_REMOVE && _hwndPanel != NULL
   && pmsg->message >= WM_KEYFIRST && pmsg->message <= WM_KEYLAST
   && pmsg->hwnd != NULL && (pmsg->hwnd == _hwndPanel || IsChild(_hwndPanel, pmsg->hwnd))) {
    BOOL bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, bAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    BOOL bText = pmsg->hwnd != _hwndList;
    BOOL bOurs;
    if (pmsg->message == WM_SYSKEYDOWN || pmsg->message == WM_SYSKEYUP || pmsg->message == WM_SYSCHAR) {
      bOurs = FALSE;
    } else if (bCtrl || bAlt) {
      UINT vk = (UINT)pmsg->wParam;
      bOurs = bText && (pmsg->message == WM_CHAR || vk == 'C' || vk == 'V' || vk == 'X' || vk == 'A' || vk == 'Z'
                     || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_HOME || vk == VK_END || vk == VK_BACK
                     || vk == VK_DELETE || vk == VK_CONTROL || vk == VK_SHIFT);
    } else if (bText) {
      bOurs = TRUE;
    } else {
      UINT vk = (UINT)pmsg->wParam;
      bOurs = pmsg->message == WM_CHAR ? FALSE
            : (vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_HOME || vk == VK_END
            || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_RETURN || vk == VK_F2 || vk == VK_SPACE);
    }
    if (bOurs) {
      TranslateMessage(pmsg);
      DispatchMessageA(pmsg);
      pmsg->message = WM_NULL;
    }
  }
  return CallNextHookEx(_hhkKeys, nCode, wParam, lParam);
}

/* ---------------------------------------------------------------------- */
/* Setting up                                                              */
/* ---------------------------------------------------------------------- */

LRESULT CALLBACK BarProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  LRESULT lResult = CallWindowProcA(_pfnOldBarProc, hwnd, uMsg, wParam, lParam);
  if (uMsg == WM_SIZE || uMsg == WM_SHOWWINDOW) LayoutPanel();
  return lResult;
}

void CreateFonts(void)
{
  NONCLIENTMETRICSA ncm;
  LOGFONTA lf;
  HDC hdc;
  TEXTMETRICA tm;
  HFONT hfontOld;
  memset(&ncm, 0, sizeof(ncm));
  ncm.cbSize = sizeof(ncm);
  if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
    lf = ncm.lfMessageFont;
  } else {
    GetObjectA(GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
  }
  _hfont = CreateFontIndirectA(&lf);
  lf.lfWeight = FW_BOLD;
  _hfontBold = CreateFontIndirectA(&lf);
  hdc = GetDC(NULL);
  hfontOld = (HFONT)SelectObject(hdc, _hfont);
  GetTextMetricsA(hdc, &tm);
  SelectObject(hdc, hfontOld);
  ReleaseDC(NULL, hdc);
  _cyRow = tm.tmHeight + 6;
}

void CreatePanel(void *pBar)
{
  WNDCLASSA wc;
  HINSTANCE hInstance = GetModuleHandleA(NULL);
  _pBar = pBar;
  _hwndBar = *(HWND *)((char *)pBar + WE_WND_HWND);
  if (_hwndBar == NULL || BarItem(IDC_PROPERTY_COMBO) == NULL) return;

  CreateFonts();
  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = PanelProc;
  wc.hInstance = hInstance;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = "AlphaFixes property panel";
  RegisterClassA(&wc);
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = ListProc;
  wc.lpszClassName = "AlphaFixes property list";
  RegisterClassA(&wc);

  _hwndPanel = CreateWindowExA(0, "AlphaFixes property panel", "", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                               0, 0, 100, 100, _hwndBar, NULL, hInstance, NULL);
  _hwndFilter = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                0, 0, 10, 10, _hwndPanel, NULL, hInstance, NULL);
  _hwndList = CreateWindowExA(WS_EX_CLIENTEDGE, "AlphaFixes property list", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP,
                              0, 0, 10, 10, _hwndPanel, NULL, hInstance, NULL);
  SendMessageA(_hwndFilter, WM_SETFONT, (WPARAM)_hfont, FALSE);
  _pfnOldFilterProc = (WNDPROC)SetWindowLongPtrA(_hwndFilter, GWLP_WNDPROC, (LONG_PTR)FilterProc);
  _pfnOldBarProc = (WNDPROC)SetWindowLongPtrA(_hwndBar, GWLP_WNDPROC, (LONG_PTR)BarProc);
  _hhkKeys = SetWindowsHookExA(WH_GETMESSAGE, KeyHook, NULL, GetCurrentThreadId());
  SetTimer(_hwndPanel, TIMER_ID, TIMER_MS, NULL);
  LayoutPanel();
  LayoutPanelChildren();
  Log("Property grid: made in the property bar");
}

/* CSize CPropertyComboBar::CalcDynamicLayout(int nLength, DWORD dwMode) */
SIZE *__fastcall Bar_CalcDynamicLayout(void *pBar, void *pvEdx, SIZE *psizeRet, int nLength, DWORD dwMode)
{
  SIZE *psize = (SIZE *)((char *)pBar + WE_BAR_SIZE);
  SIZE *ps;
  (void)pvEdx;
  if (psize->cx < MIN_BAR_WIDTH) psize->cx = MIN_BAR_WIDTH;
  if (psize->cy < MIN_BAR_HEIGHT) psize->cy = MIN_BAR_HEIGHT;
  ps = _pfnCalcDynamicLayout(pBar, NULL, psizeRet, nLength, dwMode);
  if (ps->cx < MIN_BAR_WIDTH) ps->cx = MIN_BAR_WIDTH;
  if (psize->cx < MIN_BAR_WIDTH) psize->cx = MIN_BAR_WIDTH;
  /* docked at a side, the bar takes the side's whole height (the frame gives
     side dock bars all of it) instead of its own 144 pixels */
  if ((dwMode & LM_VERTDOCK) && !(dwMode & LM_HORZDOCK)) {
    HWND hwndBar = *(HWND *)((char *)pBar + WE_WND_HWND);
    RECT rcDock;
    if (hwndBar != NULL && GetClientRect(GetParent(hwndBar), &rcDock) && rcDock.bottom - 4 > ps->cy) {
      ps->cy = rcDock.bottom - 4;
    }
  }
  if (_hwndPanel == NULL) CreatePanel(pBar);
  else LayoutPanel();
  return ps;
}

}  // namespace

bool InstallPropertyGrid()
{
  /* push ebp; mov ebp, esp; sub esp, 0x18; mov eax, [ebp+0x10]; push ebx; push esi; push edi; test al, 0x18 */
  static const BYTE aubCalcLayout[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x8B, 0x45, 0x10, 0x53, 0x56, 0x57, 0xA8, 0x18 };
  /* push ebp; mov ebp, esp; push -1; push 0x454017 */
  static const BYTE aubSetColor[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x17, 0x40, 0x45, 0x00 };
  /* call GetActiveView; test eax, eax; jnz; ret; mov eax, [eax+0x40]; ret */
  static const BYTE aubGetDocument[] = { 0xE8, 0x4B, 0x7D, 0xFF, 0xFF, 0x85, 0xC0, 0x75, 0x01, 0xC3, 0x8B, 0x40, 0x40, 0xC3 };
  static const BYTE aubUpdateData[] = { 0xFF, 0x25 };   /* jmp [import] */
  if (*WE_BAR_VTBL_CALCLAYOUT != WE_BAR_CALCLAYOUT
   || !CodeMatches((uintptr_t)WE_BAR_CALCLAYOUT, aubCalcLayout, sizeof(aubCalcLayout))
   || !CodeMatches((uintptr_t)WE_SETCOLORPROPERTY, aubSetColor, sizeof(aubSetColor))
   || !CodeMatches((uintptr_t)WE_GETDOCUMENT, aubGetDocument, sizeof(aubGetDocument))
   || !CodeMatches((uintptr_t)WE_UPDATEDATA, aubUpdateData, sizeof(aubUpdateData))) {
    return false;
  }
  void *pv = (void *)Bar_CalcDynamicLayout;
  _bInstalled = WriteCode(WE_BAR_VTBL_CALCLAYOUT, &pv, sizeof(pv));
  return _bInstalled != FALSE;
}
