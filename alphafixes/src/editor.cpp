// World Editor fixes (WorldEditor.exe, build of 13 Jan 2000).
//
// The winmm.dll proxy loads AlphaFixes.dll at WorldEditor.exe's entry point,
// the same way as in the game, and AF_Init() installs only what applies to the
// editor: the stream fix (streamfix.cpp: the editor crashed on startup on
// 64-bit Windows), the engine side of the light fix, the "Not precached"
// messages, and the fixes below.  They were SamAlphaEditorFixes.dll, which a
// patched WorldEditor.exe loaded; with AlphaFixes the exe needs no patch.
//
// Saving.  In this build CWorldEditorDoc::OnSaveDocument (0x00430300) is a
// stub that only says "Saving is disabled in this version!".  The engine part
// is still there: CWorld::Save_t is exported by Engine.dll and used by other
// editor commands.  The mod puts its own OnSaveDocument in the CWorldEditorDoc
// vtable.  It mirrors the editor's OnOpenDocument (0x00430010): make the path
// relative to the game folder with CTFileName::RemoveApplicationPath_t(), save
// the document's world (m_woWorld, at this+0x208) with CWorld::Save_t(), and
// clear the modified flag.  Engine errors (C++ exceptions carrying a char *
// message) are shown with the engine's WarningMessage(), the way the stub
// showed its message.
//
// Free camera (new).  Esc in a perspective view switches to a fly-through
// camera: the mouse looks around, WASD moves, Q/E move down/up, Shift is
// faster and the mouse wheel changes the speed.  Esc again (or switching to
// another window) goes back to normal editing.  The camera of an editor window
// is its CChildFrame's master viewer, which all four views render from.  Free
// camera moves that viewer's eye with the engine's
// CPlacement3D::Translate_OwnSystem(), like the editor's own camera commands,
// and repaints the views.  Esc reaches CWorldEditorView::OnKeyDown (Esc isn't
// an accelerator), so that message map entry is redirected here.  While
// flying, a WH_GETMESSAGE hook keeps W/A/S/D/Q/E from triggering the editor's
// accelerators and keeps the mouse away from the view's own handlers.
//
// Crash at startup when the released Serious Sam editor was used before.  The
// main window saves and restores its toolbar layout with MFC's
// SaveBarState/LoadBarState("General") under HKCU\Software\CroTeam\WorldEditor,
// the same registry key the released editor uses.  Its toolbars have other
// IDs, and MFC's SetDockState uses GetControlBar(nBarID) without a NULL check,
// so loading that editor's layout crashes in MFC42.DLL at 5F40D84D (called
// from 0x0041BB5A).  The alpha's layout goes to its own section, "Alpha
// toolbars", for both saving and loading, so neither editor reads the other's
// layout.  Before loading, every saved bar ID is checked against the bars that
// exist; an unusable layout is skipped and the default layout is used.  The
// code is shared with the Modeler, in barstate.cpp.
//
// OBJ import (new).  File > Import (and the views' backdrop object) only load
// LightWave .lwo files.  The editor's import of CObject3D::LoadAny3DFormat_t
// is redirected so that .obj files are converted to a temporary LightWave
// object first (objimport.c) and then loaded by the engine as usual.  The file
// dialogs offer *.obj and the menu item is renamed accordingly.
#include "alphafixes.h"

#include <commctrl.h>
#include <string.h>

#include "objimport.h"

namespace {

#define AFX_IDW_STATUS_BAR  0xE801   /* MFC's status bar control ID */

/* ---------------------------------------------------------------------- */
/* Saving worlds                                                           */
/* ---------------------------------------------------------------------- */

/* WorldEditor.exe, build of 13 Jan 2000 (non-relocatable, always at 0x00400000) */
#define WE_DOC_VTABLE           ((void **)0x0045D488)  /* CWorldEditorDoc vtable */
#define WE_VTBL_SETMODIFIEDFLAG 25                     /* CDocument::SetModifiedFlag(BOOL) */
#define WE_VTBL_ONSAVEDOCUMENT  32                     /* CDocument::OnSaveDocument(LPCTSTR) */
#define WE_ONSAVEDOCUMENT_STUB  ((void *)0x00430300)   /* "Saving is disabled in this version!" */
#define WE_DOC_WORLD_OFFSET     0x208                  /* CWorldEditorDoc::m_woWorld */

/* Engine's CTFileName (same layout as CTString plus one long). */
typedef struct {
  char *str_String;
  long  fnm_lReserved;
} CTFileName;

/* Engine methods are __thiscall: 'this' in ECX, arguments on the stack,
   callee pops them.  Calling them as __fastcall with a dummy EDX argument
   produces exactly that. */
typedef void (__fastcall *CTFileName_Ctor_t)(CTFileName *pfnm, void *pvEdx, const char *strName, int iOffset);
typedef void (__fastcall *CTFileName_Dtor_t)(CTFileName *pfnm, void *pvEdx);
typedef void (__fastcall *CTFileName_RemoveApplicationPath_t)(CTFileName *pfnm, void *pvEdx);
typedef void (__fastcall *CWorld_Save_t)(void *pwo, void *pvEdx, const CTFileName *pfnm);
typedef void (__fastcall *CDocument_SetModifiedFlag_t)(void *pDoc, void *pvEdx, BOOL bModified);
typedef void (__cdecl *WarningMessage_t)(const char *strFormat, ...);

CTFileName_Ctor_t                 _pfnFileNameCtor;
CTFileName_Dtor_t                 _pfnFileNameDtor;
CTFileName_RemoveApplicationPath_t _pfnRemoveApplicationPath;
CWorld_Save_t                     _pfnWorldSave;
WarningMessage_t                  _pfnWarningMessage;
BOOL                              _bSaveFixInstalled = FALSE;


BOOL SaveWorld(void *pDoc, const char *strPathName, char *strError, size_t ctError)
{
  CTFileName fnmWorld;
  volatile BOOL bConstructed = FALSE;
  volatile BOOL bSaved = FALSE;

  __try {
    _pfnFileNameCtor(&fnmWorld, NULL, strPathName, 0);
    bConstructed = TRUE;
    _pfnRemoveApplicationPath(&fnmWorld, NULL);   /* worlds must be saved inside the game folder */
    _pfnWorldSave((char *)pDoc + WE_DOC_WORLD_OFFSET, NULL, &fnmWorld);
    bSaved = TRUE;
  } __except (CatchEngineError(GetExceptionInformation(), strError, ctError, "Cannot save world: unknown error.")) {
    bSaved = FALSE;
  }
  if (bConstructed) _pfnFileNameDtor(&fnmWorld, NULL);
  return bSaved;
}

/* BOOL CWorldEditorDoc::OnSaveDocument(LPCTSTR lpszPathName) */
BOOL __fastcall WorldEditorDoc_OnSaveDocument(void *pDoc, void *pvEdx, const char *strPathName)
{
  char strError[1024];
  void **ppvVTable = *(void ***)pDoc;
  (void)pvEdx;

  if (!SaveWorld(pDoc, strPathName, strError, sizeof(strError))) {
    _pfnWarningMessage("%s", strError);
    return FALSE;
  }
  ((CDocument_SetModifiedFlag_t)ppvVTable[WE_VTBL_SETMODIFIEDFLAG])(pDoc, NULL, FALSE);
  return TRUE;
}



void InstallSaveFix(void)
{
  if (WE_DOC_VTABLE[WE_VTBL_ONSAVEDOCUMENT] != WE_ONSAVEDOCUMENT_STUB) return;

  _pfnFileNameCtor          = (CTFileName_Ctor_t)EngineProc("??0CTFileName@@QAE@PBDH@Z");
  _pfnFileNameDtor          = (CTFileName_Dtor_t)EngineProc("??1CTFileName@@QAE@XZ");
  _pfnRemoveApplicationPath = (CTFileName_RemoveApplicationPath_t)EngineProc("?RemoveApplicationPath_t@CTFileName@@QAEXXZ");
  _pfnWorldSave             = (CWorld_Save_t)EngineProc("?Save_t@CWorld@@QAEXABVCTFileName@@@Z");
  _pfnWarningMessage        = (WarningMessage_t)EngineProc("?WarningMessage@@YAXPBDZZ");
  if (_pfnFileNameCtor == NULL || _pfnFileNameDtor == NULL || _pfnRemoveApplicationPath == NULL
   || _pfnWorldSave == NULL || _pfnWarningMessage == NULL) {
    return;
  }

  _bSaveFixInstalled = SetVTableSlot(&WE_DOC_VTABLE[WE_VTBL_ONSAVEDOCUMENT], (void *)WorldEditorDoc_OnSaveDocument);
}



/* ---------------------------------------------------------------------- */
/* Free camera                                                             */
/* ---------------------------------------------------------------------- */

/* Each editor window (CChildFrame) owns one master viewer that all four
   views of that window render from.  Its first member is the camera (eye)
   placement; the orbit target sits mv_fTargetDistance in front of it. */
#define WE_FRAME_MASTERVIEWER   0x200                  /* CChildFrame::m_mvViewer */
#define WE_VIEW_HWND            0x20                   /* CWnd::m_hWnd */
#define WE_VIEW_DOCUMENT        0x40                   /* CView::m_pDocument */
#define WE_VIEW_PROJECTION      0x12C                  /* CWorldEditorView projection type */
#define WE_PROJECTION_PERSPECTIVE 1
#define WE_VIEW_KEYDOWN_ENTRY   ((void **)0x0045D61C)  /* message map: WM_KEYDOWN -> pfn */
#define WE_VIEW_ONKEYDOWN       ((void *)0x0043C2F0)   /* CWorldEditorView::OnKeyDown */
#define WE_GETPARENTFRAME       ((void *)0x00442850)   /* CWnd::GetParentFrame thunk */
#define WE_UPDATEALLVIEWS       ((void *)0x004502DC)   /* CDocument::UpdateAllViews thunk */

#define FREECAM_TIMER_ID        0x5A11
#define FREECAM_TICK_MS         10
/* Base speed follows the scale of what is being looked at: a quarter of the
   distance to the camera's orbit target per second.  The mouse wheel scales it. */
#define FREECAM_SPEED_PER_DISTANCE 0.25f
#define FREECAM_BASE_SPEED_MIN  2.0f     /* metres per second */
#define FREECAM_BASE_SPEED_MAX  500.0f
#define FREECAM_WHEEL_MIN       0.02f
#define FREECAM_WHEEL_MAX       50.0f
#define FREECAM_FAST_FACTOR     4.0f     /* while Shift is held */
#define FREECAM_MOUSE_DEGREES   0.15f    /* per pixel of mouse movement */
#define FREECAM_OTHERVIEWS_MS   200      /* how often the other three views follow */

typedef struct {
  float vPosition[3];       /* x, y (up), z */
  float aOrientation[3];    /* heading, pitch, banking in degrees */
} CPlacement3D;

typedef struct {
  CPlacement3D mv_plViewer;
  float        mv_fTargetDistance;
} CMasterViewer;

typedef void  (__fastcall *View_OnKeyDown_t)(void *pView, void *pvEdx, UINT nChar, UINT nRepCnt, UINT nFlags);
typedef void *(__fastcall *Wnd_GetParentFrame_t)(void *pWnd, void *pvEdx);
typedef void  (__fastcall *Document_UpdateAllViews_t)(void *pDoc, void *pvEdx, void *pSender, LPARAM lHint, void *pHint);
typedef void  (__fastcall *Placement_TranslateOwnSystem_t)(CPlacement3D *ppl, void *pvEdx, const float *pvDelta);

Placement_TranslateOwnSystem_t _pfnTranslateOwnSystem;
BOOL _bFreeCamInstalled = FALSE;

struct {
  BOOL   bActive;
  void  *pView;
  HWND   hwndView;
  HWND   hwndStatusBar;
  char   strOldStatus[256];
  HHOOK  hhkMessages;
  POINT  ptOldCursor;
  float  fBaseSpeed;        /* from the orbit distance when free cam started */
  float  fWheelFactor;      /* mouse wheel; kept between uses */
  LARGE_INTEGER llLastTick;
  LARGE_INTEGER llLastOtherViews;
  LARGE_INTEGER llFrequency;
} _fc = { FALSE, NULL, NULL, NULL, "", NULL, { 0, 0 }, 16.0f, 1.0f };

BOOL IsFreeCamKey(WPARAM vk)
{
  return vk == 'W' || vk == 'A' || vk == 'S' || vk == 'D' || vk == 'Q' || vk == 'E';
}

BOOL IsKeyDown(int vk)
{
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

/* The view or the engine's "ViewPort Window" that covers it. */
BOOL IsInView(HWND hwnd)
{
  return hwnd != NULL && (hwnd == _fc.hwndView || IsChild(_fc.hwndView, hwnd));
}

CMasterViewer *GetMasterViewer(void *pView)
{
  char *pFrame = (char *)((Wnd_GetParentFrame_t)WE_GETPARENTFRAME)(pView, NULL);
  return (pFrame != NULL) ? (CMasterViewer *)(pFrame + WE_FRAME_MASTERVIEWER) : NULL;
}

void UpdateAllViews(void *pView, void *pSender)
{
  void *pDoc = *(void **)((char *)pView + WE_VIEW_DOCUMENT);
  if (pDoc != NULL) ((Document_UpdateAllViews_t)WE_UPDATEALLVIEWS)(pDoc, NULL, pSender, 0, NULL);
}

POINT GetViewCenter(HWND hwnd)
{
  RECT rc;
  POINT pt;
  GetClientRect(hwnd, &rc);
  pt.x = (rc.left + rc.right) / 2;
  pt.y = (rc.top + rc.bottom) / 2;
  ClientToScreen(hwnd, &pt);
  return pt;
}

void ClipCursorToView(HWND hwnd)
{
  RECT rc;
  GetClientRect(hwnd, &rc);
  MapWindowPoints(hwnd, NULL, (POINT *)&rc, 2);
  ClipCursor(&rc);
}

void ExitFreeCam(void);

void ShowFreeCamStatus(void)
{
  char strStatus[256];
  if (_fc.hwndStatusBar == NULL || !IsWindow(_fc.hwndStatusBar)) return;
  wsprintfA(strStatus, "Free camera (%d m/s): mouse to look, WASD to move, Q/E down/up, "
    "Shift faster, wheel changes speed, Esc to exit", (int)(_fc.fBaseSpeed * _fc.fWheelFactor + 0.5f));
  SendMessageA(_fc.hwndStatusBar, SB_SETTEXTA, 0, (LPARAM)strStatus);
}

/* The editor rewrites the status text when modifier keys change (Shift...). Put ours back. */
void KeepFreeCamStatus(void)
{
  char strCurrent[256];
  if (_fc.hwndStatusBar == NULL || !IsWindow(_fc.hwndStatusBar)) return;
  if (LOWORD(SendMessageA(_fc.hwndStatusBar, SB_GETTEXTLENGTHA, 0, 0)) >= sizeof(strCurrent)) return;
  SendMessageA(_fc.hwndStatusBar, SB_GETTEXTA, 0, (LPARAM)strCurrent);
  if (strncmp(strCurrent, "Free camera", 11) != 0) ShowFreeCamStatus();
}

/* While flying, keep the movement keys away from the editor's accelerators
   (W, S and D are editor commands) and the mouse away from the view. */
LRESULT CALLBACK FreeCamMessageHook(int nCode, WPARAM wParam, LPARAM lParam)
{
  if (nCode == HC_ACTION && _fc.bActive) {
    MSG *pmsg = (MSG *)lParam;
    if (!IsWindow(_fc.hwndView)) {
      ExitFreeCam();
    } else {
      switch (pmsg->message) {
      case WM_KEYDOWN:
      case WM_KEYUP:
        if (IsFreeCamKey(pmsg->wParam)) pmsg->message = WM_NULL;
        break;
      case WM_MOUSEWHEEL:
        if (wParam == PM_REMOVE) {
          short sDelta = (short)HIWORD(pmsg->wParam);
          _fc.fWheelFactor *= (sDelta > 0) ? 1.25f : 0.8f;
          if (_fc.fWheelFactor < FREECAM_WHEEL_MIN) _fc.fWheelFactor = FREECAM_WHEEL_MIN;
          if (_fc.fWheelFactor > FREECAM_WHEEL_MAX) _fc.fWheelFactor = FREECAM_WHEEL_MAX;
          ShowFreeCamStatus();
        }
        pmsg->message = WM_NULL;
        break;
      case WM_MOUSEMOVE:
      case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
      case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
      case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        if (IsInView(pmsg->hwnd)) pmsg->message = WM_NULL;
        break;
      }
    }
  }
  return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void CALLBACK FreeCamTick(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
  CMasterViewer *pmv;
  LARGE_INTEGER llNow;
  POINT ptCenter, ptCursor;
  float fDelta, fStep, vMove[3];
  (void)uMsg; (void)idEvent; (void)dwTime;

  if (!_fc.bActive || hwnd != _fc.hwndView) return;
  /* leave when the view loses focus or stops being a perspective view */
  if (!IsInView(GetFocus())
   || *(int *)((char *)_fc.pView + WE_VIEW_PROJECTION) != WE_PROJECTION_PERSPECTIVE) {
    ExitFreeCam();
    return;
  }
  pmv = GetMasterViewer(_fc.pView);
  if (pmv == NULL) { ExitFreeCam(); return; }

  QueryPerformanceCounter(&llNow);
  fDelta = (float)(llNow.QuadPart - _fc.llLastTick.QuadPart) / (float)_fc.llFrequency.QuadPart;
  _fc.llLastTick = llNow;
  if (fDelta > 0.1f) fDelta = 0.1f;

  /* mouse look: measure how far the cursor moved from the view center, then re-center it */
  ptCenter = GetViewCenter(_fc.hwndView);
  GetCursorPos(&ptCursor);
  if (ptCursor.x != ptCenter.x || ptCursor.y != ptCenter.y) {
    float *pa = pmv->mv_plViewer.aOrientation;
    pa[0] -= (ptCursor.x - ptCenter.x) * FREECAM_MOUSE_DEGREES;   /* heading: positive turns left */
    pa[1] -= (ptCursor.y - ptCenter.y) * FREECAM_MOUSE_DEGREES;   /* pitch: positive looks up */
    if (pa[0] >= 180.0f) pa[0] -= 360.0f;
    if (pa[0] < -180.0f) pa[0] += 360.0f;
    if (pa[1] > 90.0f) pa[1] = 90.0f;
    if (pa[1] < -90.0f) pa[1] = -90.0f;
    SetCursorPos(ptCenter.x, ptCenter.y);
  }

  /* WASD moves in the camera's own system (-Z is forward), Q/E go straight down/up */
  fStep = _fc.fBaseSpeed * _fc.fWheelFactor * fDelta * (IsKeyDown(VK_SHIFT) ? FREECAM_FAST_FACTOR : 1.0f);
  vMove[0] = (IsKeyDown('D') - IsKeyDown('A')) * fStep;
  vMove[1] = 0.0f;
  vMove[2] = (IsKeyDown('S') - IsKeyDown('W')) * fStep;
  if (vMove[0] != 0.0f || vMove[2] != 0.0f) _pfnTranslateOwnSystem(&pmv->mv_plViewer, NULL, vMove);
  pmv->mv_plViewer.vPosition[1] += (IsKeyDown('E') - IsKeyDown('Q')) * fStep;

  KeepFreeCamStatus();

  /* redraw this view now, the other three now and then */
  InvalidateRect(_fc.hwndView, NULL, FALSE);
  UpdateWindow(_fc.hwndView);
  if ((llNow.QuadPart - _fc.llLastOtherViews.QuadPart) * 1000 > _fc.llFrequency.QuadPart * FREECAM_OTHERVIEWS_MS) {
    _fc.llLastOtherViews = llNow;
    UpdateAllViews(_fc.pView, _fc.pView);
  }
}

void EnterFreeCam(void *pView)
{
  HWND hwnd = *(HWND *)((char *)pView + WE_VIEW_HWND);
  CMasterViewer *pmv = GetMasterViewer(pView);
  POINT ptCenter;

  if (_fc.bActive || !IsWindow(hwnd) || pmv == NULL) return;

  _fc.fBaseSpeed = pmv->mv_fTargetDistance * FREECAM_SPEED_PER_DISTANCE;
  if (!(_fc.fBaseSpeed >= FREECAM_BASE_SPEED_MIN)) _fc.fBaseSpeed = FREECAM_BASE_SPEED_MIN;   /* also catches NaN */
  if (_fc.fBaseSpeed > FREECAM_BASE_SPEED_MAX) _fc.fBaseSpeed = FREECAM_BASE_SPEED_MAX;

  _fc.hhkMessages = SetWindowsHookExA(WH_GETMESSAGE, FreeCamMessageHook, NULL, GetCurrentThreadId());
  if (_fc.hhkMessages == NULL) return;
  if (!SetTimer(hwnd, FREECAM_TIMER_ID, FREECAM_TICK_MS, FreeCamTick)) {
    UnhookWindowsHookEx(_fc.hhkMessages);
    _fc.hhkMessages = NULL;
    return;
  }

  _fc.bActive = TRUE;
  _fc.pView = pView;
  _fc.hwndView = hwnd;
  QueryPerformanceFrequency(&_fc.llFrequency);
  QueryPerformanceCounter(&_fc.llLastTick);
  _fc.llLastOtherViews = _fc.llLastTick;

  GetCursorPos(&_fc.ptOldCursor);
  ptCenter = GetViewCenter(hwnd);
  SetCursorPos(ptCenter.x, ptCenter.y);
  ClipCursorToView(hwnd);
  ShowCursor(FALSE);

  /* show the controls in the main window's status bar */
  _fc.hwndStatusBar = GetDlgItem(GetAncestor(hwnd, GA_ROOT), AFX_IDW_STATUS_BAR);
  _fc.strOldStatus[0] = 0;
  if (_fc.hwndStatusBar != NULL) {
    if (LOWORD(SendMessageA(_fc.hwndStatusBar, SB_GETTEXTLENGTHA, 0, 0)) < sizeof(_fc.strOldStatus)) {
      SendMessageA(_fc.hwndStatusBar, SB_GETTEXTA, 0, (LPARAM)_fc.strOldStatus);
    }
    ShowFreeCamStatus();
  }
}

void ExitFreeCam(void)
{
  if (!_fc.bActive) return;
  _fc.bActive = FALSE;

  if (IsWindow(_fc.hwndView)) KillTimer(_fc.hwndView, FREECAM_TIMER_ID);
  if (_fc.hhkMessages != NULL) {
    UnhookWindowsHookEx(_fc.hhkMessages);
    _fc.hhkMessages = NULL;
  }
  ShowCursor(TRUE);
  ClipCursor(NULL);
  SetCursorPos(_fc.ptOldCursor.x, _fc.ptOldCursor.y);

  if (_fc.hwndStatusBar != NULL && IsWindow(_fc.hwndStatusBar)) {
    SendMessageA(_fc.hwndStatusBar, SB_SETTEXTA, 0, (LPARAM)_fc.strOldStatus);
  }
  _fc.hwndStatusBar = NULL;

  /* bring all views of the window up to date with the final camera position */
  if (IsWindow(_fc.hwndView)) UpdateAllViews(_fc.pView, NULL);
  _fc.pView = NULL;
  _fc.hwndView = NULL;
}

/* Replaces CWorldEditorView::OnKeyDown in the view's message map. */
void __fastcall WorldEditorView_OnKeyDown(void *pView, void *pvEdx, UINT nChar, UINT nRepCnt, UINT nFlags)
{
  (void)pvEdx;
  if (nChar == VK_ESCAPE && !(nFlags & KF_REPEAT)) {
    if (_fc.bActive) {
      ExitFreeCam();
      return;
    }
    if (*(int *)((char *)pView + WE_VIEW_PROJECTION) == WE_PROJECTION_PERSPECTIVE) {
      EnterFreeCam(pView);
      return;
    }
  }
  ((View_OnKeyDown_t)WE_VIEW_ONKEYDOWN)(pView, NULL, nChar, nRepCnt, nFlags);
}

void InstallFreeCam(void)
{
  if (*WE_VIEW_KEYDOWN_ENTRY != WE_VIEW_ONKEYDOWN) return;

  _pfnTranslateOwnSystem = (Placement_TranslateOwnSystem_t)EngineProc("?Translate_OwnSystem@CPlacement3D@@QAEXABV?$Vector@M$02@@@Z");
  if (_pfnTranslateOwnSystem == NULL) return;

  _bFreeCamInstalled = SetVTableSlot(WE_VIEW_KEYDOWN_ENTRY, (void *)WorldEditorView_OnKeyDown);
}



/* ---------------------------------------------------------------------- */
/* Toolbar layout shared with the released editor (barstate.cpp)           */
/* ---------------------------------------------------------------------- */

#define WE_LOADBAR_PUSH         0x0041BB53   /* push offset "General" */
#define WE_LOADBAR_CALL         0x0041BB5A   /* call CFrameWnd::LoadBarState */
#define WE_SAVEBAR_PUSH         0x0041CBDA   /* push offset "General" */

const BYTE _aubLoadPushOld[5] = { 0x68, 0x1C, 0xCA, 0x46, 0x00 };
const BYTE _aubLoadCallOld[5] = { 0xE8, 0x79, 0x48, 0x03, 0x00 };
const BYTE _aubSavePushOld[5] = { 0x68, 0x24, 0xD0, 0x46, 0x00 };



/* ---------------------------------------------------------------------- */
/* OBJ import                                                              */
/* ---------------------------------------------------------------------- */

/* The editor loads 3D objects (File > Import, view backdrop objects) through
   CObject3D::LoadAny3DFormat_t, which only knows LightWave (.lwo) and .map.
   The editor's import table entry for it is redirected here: .obj files are
   converted to a temporary LightWave object (objimport.c), which the engine
   then loads as usual.  Everything else goes straight to the engine. */
#define WE_IAT_LOADANY3DFORMAT  ((void **)0x0045720C)  /* import slot of CObject3D::LoadAny3DFormat_t */
#define WE_IMPORT_FILTER_PUSH   ((BYTE *)0x0042EA1E)   /* push offset "Lightwave object (*.lwo)..." */
#define WE_BACKDROP_FILTER_PUSH ((BYTE *)0x00419CC6)   /* push offset "3D object (*.lwo)..." */
#define WE_ID_IMPORT_3D         0x8045                 /* File > Import LWO */
#define OBJ_TEMP_LWO            "Temp\\AlphaFixes_ObjImport.lwo"   /* relative to the game folder */

typedef void (__fastcall *Object3D_LoadAny3DFormat_t)(void *po3d, void *pvEdx, const CTFileName *pfnm, float fStretch);
typedef void (__cdecl *ThrowF_t)(const char *strFormat, ...);

const char _strImportFilter[] =
  "3D objects (*.lwo, *.obj)\0*.lwo;*.obj\0Lightwave object (*.lwo)\0*.lwo\0"
  "Wavefront object (*.obj)\0*.obj\0All files (*.*)\0*.*\0";
const char _strBackdropFilter[] =
  "3D object (*.lwo, *.obj)\0*.lwo;*.obj\0All files (*.*)\0*.*\0";
const char _strImportMenuText[] = "&Import 3D object (LWO, OBJ)...";

const BYTE _aubImportPushOld[5]   = { 0x68, 0xE8, 0xF4, 0x46, 0x00 };
const BYTE _aubBackdropPushOld[5] = { 0x68, 0x28, 0xC8, 0x46, 0x00 };

Object3D_LoadAny3DFormat_t _pfnLoadAny3DFormat;
CTFileName_Ctor_t _pfnObjFileNameCtor;
CTFileName_Dtor_t _pfnObjFileNameDtor;
ThrowF_t          _pfnThrowF;
const CTFileName *_pfnmApplicationPath;
HHOOK             _hhkMenuRename;
BOOL              _bObjImportInstalled = FALSE;

BOOL EndsWithNoCase(const char *str, const char *strEnd)
{
  size_t ct = strlen(str), ctEnd = strlen(strEnd);
  return ct >= ctEnd && lstrcmpiA(str + ct - ctEnd, strEnd) == 0;
}

/* Replaces CObject3D::LoadAny3DFormat_t(const CTFileName &fnm, FLOAT fStretch) for the editor. */
void __fastcall Object3D_LoadAny3DFormat(void *po3d, void *pvEdx, const CTFileName *pfnm, float fStretch)
{
  const char *strFile = pfnm->str_String;
  const char *strGame = _pfnmApplicationPath->str_String;
  char strObj[MAX_PATH * 2], strTempDir[MAX_PATH * 2], strLwo[MAX_PATH * 2], strError[1024];
  CTFileName fnmLwo;
  (void)pvEdx;

  if (strFile == NULL || !EndsWithNoCase(strFile, ".obj")) {
    _pfnLoadAny3DFormat(po3d, NULL, pfnm, fStretch);
    return;
  }

  /* editor file names are relative to the game folder */
  if ((strFile[0] != 0 && strFile[1] == ':') || (strFile[0] == '\\' && strFile[1] == '\\')) {
    lstrcpynA(strObj, strFile, sizeof(strObj));
  } else {
    wsprintfA(strObj, "%s%s", strGame, strFile);
  }
  wsprintfA(strTempDir, "%sTemp", strGame);
  CreateDirectoryA(strTempDir, NULL);
  wsprintfA(strLwo, "%s%s", strGame, OBJ_TEMP_LWO);

  if (!ObjToLwo(strObj, strLwo, strGame, NULL, strError, sizeof(strError))) {
    DeleteFileA(strLwo);
    _pfnThrowF("%s", strError);   /* engine error, shown by the editor; does not return */
    return;
  }

  _pfnObjFileNameCtor(&fnmLwo, NULL, OBJ_TEMP_LWO, 0);
  __try {
    _pfnLoadAny3DFormat(po3d, NULL, &fnmLwo, fStretch);
  } __finally {
    _pfnObjFileNameDtor(&fnmLwo, NULL);
    DeleteFileA(strLwo);
  }
}

/* "Import LWO" in the File menus becomes "Import 3D object (LWO, OBJ)". */
LRESULT CALLBACK MenuRenameHook(int nCode, WPARAM wParam, LPARAM lParam)
{
  if (nCode == HC_ACTION) {
    const CWPSTRUCT *pcwp = (const CWPSTRUCT *)lParam;
    if (pcwp->message == WM_INITMENUPOPUP && IsMenu((HMENU)pcwp->wParam)) {
      HMENU hMenu = (HMENU)pcwp->wParam;
      int i, ct = GetMenuItemCount(hMenu);
      for (i = 0; i < ct; i++) {
        char strText[64];
        if (GetMenuItemID(hMenu, i) != WE_ID_IMPORT_3D) continue;
        GetMenuStringA(hMenu, i, strText, sizeof(strText), MF_BYPOSITION);
        if (strcmp(strText, _strImportMenuText) != 0) {
          ModifyMenuA(hMenu, i, MF_BYPOSITION | MF_STRING, WE_ID_IMPORT_3D, _strImportMenuText);
        }
      }
    }
  }
  return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void InstallObjImport(void)
{
  void *pvLoadAny3DFormat = (void *)EngineProc("?LoadAny3DFormat_t@CObject3D@@QAEXABVCTFileName@@M@Z");
  BYTE aubPush[5];
  DWORD dwFilter;

  if (pvLoadAny3DFormat == NULL) return;
  if (*WE_IAT_LOADANY3DFORMAT != pvLoadAny3DFormat
   || memcmp(WE_IMPORT_FILTER_PUSH, _aubImportPushOld, 5) != 0
   || memcmp(WE_BACKDROP_FILTER_PUSH, _aubBackdropPushOld, 5) != 0) {
    return;
  }

  _pfnObjFileNameCtor   = (CTFileName_Ctor_t)EngineProc("??0CTFileName@@QAE@PBDH@Z");
  _pfnObjFileNameDtor   = (CTFileName_Dtor_t)EngineProc("??1CTFileName@@QAE@XZ");
  _pfnThrowF            = (ThrowF_t)EngineProc("?ThrowF_t@@YAXPADZZ");
  _pfnmApplicationPath  = (const CTFileName *)EngineProc("?_fnmApplicationPath@@3VCTFileName@@A");
  if (_pfnObjFileNameCtor == NULL || _pfnObjFileNameDtor == NULL || _pfnThrowF == NULL || _pfnmApplicationPath == NULL) {
    return;
  }
  _pfnLoadAny3DFormat = (Object3D_LoadAny3DFormat_t)pvLoadAny3DFormat;

  if (!SetVTableSlot(WE_IAT_LOADANY3DFORMAT, (void *)Object3D_LoadAny3DFormat)) return;
  _bObjImportInstalled = TRUE;

  /* offer .obj in the file dialogs */
  aubPush[0] = 0x68;
  dwFilter = (DWORD)(DWORD_PTR)_strImportFilter;
  memcpy(aubPush + 1, &dwFilter, 4);
  WriteCode(WE_IMPORT_FILTER_PUSH, aubPush, 5);
  dwFilter = (DWORD)(DWORD_PTR)_strBackdropFilter;
  memcpy(aubPush + 1, &dwFilter, 4);
  WriteCode(WE_BACKDROP_FILTER_PUSH, aubPush, 5);

  _hhkMenuRename = SetWindowsHookExA(WH_CALLWNDPROC, MenuRenameHook, NULL, GetCurrentThreadId());
}

/* ---------------------------------------------------------------------- */
/* Entities.dll                                                            */
/* ---------------------------------------------------------------------- */

/* The game installs its Entities.dll fixes from its frame loop once the DLL
   is there; the editor has no such loop, so the engine's LoadLibraryA import
   tells when it loads Entities.dll (for the first world or class). */
typedef HMODULE (WINAPI *LoadLibraryA_t)(LPCSTR strFile);
LoadLibraryA_t _pfnEngineLoadLibraryA = NULL;

HMODULE WINAPI EngineLoadLibraryA(LPCSTR strFile)
{
  HMODULE hModule = _pfnEngineLoadLibraryA(strFile);
  if (hModule != NULL && hModule == GetModuleHandleA("Entities.dll")) InstallEditorEntitiesFixes(hModule);
  return hModule;
}

}  // namespace

void InstallEditorFixes()
{
  InstallSaveFix();
  Log("Saving: %s", _bSaveFixInstalled ? "installed" : "code differs, not installed");
  InstallFreeCam();
  Log("Free camera: %s", _bFreeCamInstalled ? "installed" : "code differs, not installed");
  bool bBarState = InstallBarStateFix(WE_LOADBAR_PUSH, WE_LOADBAR_CALL, WE_SAVEBAR_PUSH,
                                      _aubLoadPushOld, _aubLoadCallOld, _aubSavePushOld);
  Log("Toolbar layout: %s", bBarState ? "installed" : "code differs, not installed");
  InstallObjImport();
  Log("OBJ import: %s", _bObjImportInstalled ? "installed" : "code differs, not installed");
  if (g_cfg.bPropertyGrid) Log("Property grid: %s", InstallPropertyGrid() ? "installed" : "code differs, not installed");
  else Log("Property grid: off");
  _pfnEngineLoadLibraryA = (LoadLibraryA_t)HookImport(GetModuleHandleA("Engine.dll"), "KERNEL32.dll", "LoadLibraryA",
                                                      (void *)EngineLoadLibraryA);
  if (_pfnEngineLoadLibraryA == NULL) Log("Entities.dll fixes: the engine's LoadLibraryA import not found, not installed");
}
