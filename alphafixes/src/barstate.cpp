// Toolbar layouts shared with the released Serious Sam tools (World Editor and Modeler).
//
// The alpha World Editor and Modeler keep their toolbar layout with MFC's
// SaveBarState/LoadBarState("General") under HKCU\Software\CroTeam\WorldEditor
// and ...\Modeler, the same registry keys the released (First Encounter) tools
// use.  Their toolbars have other IDs, and MFC's CFrameWnd::SetDockState uses
// GetControlBar(nBarID) without a NULL check, so a layout saved by a released
// tool crashes the alpha one in MFC42.DLL (5F40D84D) right after its window
// appears.  Whether it crashes depends on which version ran last.
//
// The alpha layout goes to its own section, "Alpha toolbars", for both saving
// and loading, so neither version reads the other's layout.  Before loading,
// every saved bar ID is checked against the bars that exist; an unusable
// layout is skipped and the program starts with its default layout.
//
// The MFC functions are taken from MFC42.DLL by ordinal, the same entries the
// programs import (LoadBarState 4146, AfxGetModuleState 1168,
// CWinApp::GetProfileInt 3521).
#include "alphafixes.h"

namespace {

constexpr int MFC_LOADBARSTATE = 4146;        // CFrameWnd::LoadBarState(LPCTSTR)
constexpr int MFC_AFXGETMODULESTATE = 1168;   // AfxGetModuleState()
constexpr int MFC_GETPROFILEINT = 3521;       // CWinApp::GetProfileInt(LPCTSTR, LPCTSTR, int)
constexpr size_t WND_HWND = 0x20;             // CWnd::m_hWnd
constexpr size_t MODULESTATE_APP = 4;         // AFX_MODULE_STATE::m_pCurrentWinApp
constexpr int MAX_SAVED_BARS = 256;

using Frame_LoadBarState_t = void(__fastcall *)(void *pFrame, void *pvEdx, const char *strProfileName);
using AfxGetModuleState_t = void *(__cdecl *)();
using App_GetProfileInt_t = UINT(__fastcall *)(void *pApp, void *pvEdx, const char *strSection, const char *strEntry, int iDefault);

Frame_LoadBarState_t _pfnLoadBarState = nullptr;
AfxGetModuleState_t _pfnAfxGetModuleState = nullptr;
App_GetProfileInt_t _pfnGetProfileInt = nullptr;

// the alpha's own section name, so neither version reads the other's layout
const char _strBarSection[] = "Alpha toolbars";

struct BarSearch {
  HWND hwndFrame;
  UINT nID;
  BOOL bFound;
};

BOOL CALLBACK FindBarInChildren(HWND hwnd, LPARAM lParam)
{
  BarSearch *pbs = (BarSearch *)lParam;
  if ((UINT)GetDlgCtrlID(hwnd) == pbs->nID) pbs->bFound = TRUE;
  return !pbs->bFound;
}

BOOL CALLBACK FindBarInThreadWindows(HWND hwnd, LPARAM lParam)
{
  BarSearch *pbs = (BarSearch *)lParam;
  // the main frame, or one of its floating toolbar frames
  if (hwnd == pbs->hwndFrame || GetWindow(hwnd, GW_OWNER) == pbs->hwndFrame) {
    EnumChildWindows(hwnd, FindBarInChildren, lParam);
  }
  return !pbs->bFound;
}

// Same answer as CFrameWnd::GetControlBar(nID) != NULL: control bars are
// windows with the bar ID as their control ID.
BOOL FrameHasControlBar(HWND hwndFrame, UINT nID)
{
  BarSearch bs = { hwndFrame, nID, FALSE };
  EnumThreadWindows(GetCurrentThreadId(), FindBarInThreadWindows, (LPARAM)&bs);
  return bs.bFound;
}

// Read the layout the way CDockState::LoadState does and check that every
// docked bar it names exists, before MFC gets to use it.
BOOL IsBarStateUsable(void *pFrame, const char *strSection)
{
  HWND hwndFrame = *(HWND *)((char *)pFrame + WND_HWND);
  void *pModuleState = _pfnAfxGetModuleState();
  void *pApp = (pModuleState != nullptr) ? *(void **)((char *)pModuleState + MODULESTATE_APP) : nullptr;
  char strKey[128], strEntry[32];

  if (pApp == nullptr || !IsWindow(hwndFrame)) return FALSE;

  wsprintfA(strKey, "%s-Summary", strSection);
  int ctBars = (int)_pfnGetProfileInt(pApp, nullptr, strKey, "Bars", 0);
  if (ctBars <= 0) return TRUE;                  // nothing saved yet
  if (ctBars > MAX_SAVED_BARS) return FALSE;

  for (int iBar = 0; iBar < ctBars; iBar++) {
    wsprintfA(strKey, "%s-Bar%d", strSection, iBar);
    // floating bars get a new frame from MFC; only docked ones are looked up
    if (!_pfnGetProfileInt(pApp, nullptr, strKey, "Floating", FALSE)
     && !FrameHasControlBar(hwndFrame, _pfnGetProfileInt(pApp, nullptr, strKey, "BarID", 0))) {
      return FALSE;
    }
    // bars listed inside a dock bar; a non-zero high word marks a placeholder
    int ctIDs = (int)_pfnGetProfileInt(pApp, nullptr, strKey, "Bars", 0);
    if (ctIDs < 0 || ctIDs > MAX_SAVED_BARS) return FALSE;
    for (int iID = 0; iID < ctIDs; iID++) {
      wsprintfA(strEntry, "Bar#%d", iID);
      UINT nID = _pfnGetProfileInt(pApp, nullptr, strKey, strEntry, 0);
      if (nID != 0 && HIWORD(nID) == 0 && !FrameHasControlBar(hwndFrame, nID)) return FALSE;
    }
  }
  return TRUE;
}

// Replaces the main frame's call to LoadBarState(). An unusable layout is
// skipped, so the program starts with its default toolbar layout instead.
void __fastcall MainFrame_LoadBarState(void *pFrame, void *pvEdx, const char *strSection)
{
  (void)pvEdx;
  if (IsBarStateUsable(pFrame, strSection)) _pfnLoadBarState(pFrame, nullptr, strSection);
}

}  // namespace

bool InstallBarStateFix(uintptr_t addrLoadPush, uintptr_t addrLoadCall, uintptr_t addrSavePush,
                        const BYTE aubLoadPushOld[5], const BYTE aubLoadCallOld[5], const BYTE aubSavePushOld[5])
{
  HMODULE hMfc = GetModuleHandleA("MFC42.DLL");
  if (hMfc == nullptr) return false;
  _pfnLoadBarState = (Frame_LoadBarState_t)GetProcAddress(hMfc, MAKEINTRESOURCEA(MFC_LOADBARSTATE));
  _pfnAfxGetModuleState = (AfxGetModuleState_t)GetProcAddress(hMfc, MAKEINTRESOURCEA(MFC_AFXGETMODULESTATE));
  _pfnGetProfileInt = (App_GetProfileInt_t)GetProcAddress(hMfc, MAKEINTRESOURCEA(MFC_GETPROFILEINT));
  if (_pfnLoadBarState == nullptr || _pfnAfxGetModuleState == nullptr || _pfnGetProfileInt == nullptr) return false;

  if (!CodeMatches(addrLoadPush, aubLoadPushOld, 5) || !CodeMatches(addrLoadCall, aubLoadCallOld, 5)
   || !CodeMatches(addrSavePush, aubSavePushOld, 5)) {
    return false;
  }

  BYTE aubPush[5], aubCall[5];
  DWORD dwSection = (DWORD)(DWORD_PTR)_strBarSection;
  LONG slRelative = (LONG)((DWORD_PTR)MainFrame_LoadBarState - (DWORD_PTR)(addrLoadCall + 5));
  aubPush[0] = 0x68;                             // push offset _strBarSection
  memcpy(aubPush + 1, &dwSection, 4);
  aubCall[0] = 0xE8;                             // call MainFrame_LoadBarState
  memcpy(aubCall + 1, &slRelative, 4);

  return WriteCode((void *)addrSavePush, aubPush, 5)
      && WriteCode((void *)addrLoadPush, aubPush, 5)
      && WriteCode((void *)addrLoadCall, aubCall, 5);
}
