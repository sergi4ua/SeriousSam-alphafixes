/*
 * SamAlphaFix.dll - fixes for the Serious Sam alpha World Editor
 * (WorldEditor.exe, build of 13 Jan 2000).
 *
 * WorldEditor.exe is patched (see PatchWorldEditor.c) to LoadLibrary this DLL
 * immediately after it calls CTStream::EnableStreamHandling().
 *
 *
 * 1. Crash on 64-bit Windows (Vista and later)
 * ============================================
 * The alpha Serious Engine reads files through "virtual memory streams": the
 * whole file gets a reserved (but uncommitted) address range, and pages are
 * committed lazily.  The first touch of an uncommitted page raises an access
 * violation, which CTStream::ExceptionFilter() catches.  The filter commits
 * the page, fills it from disk, and returns EXCEPTION_CONTINUE_EXECUTION.
 *
 * WorldEditor.exe installs that filter with __try/__except around
 * CWinApp::Run(), meaning it is the outermost frame of the message loop.  Nearly
 * all editor work (loading the virtual tree, worlds, textures...) runs inside
 * window procedures that user32 calls back from the kernel.  On WOW64 (32-bit
 * programs on 64-bit Windows) an exception can't unwind through that
 * kernel->user callback boundary.  The __except filter in Run() is never
 * reached, so the first page fault in a stream crashes the editor
 * (ACCESS_VIOLATION in CTStream::GetID_t, see WorldEditor.RPT).
 *
 * Fix: a vectored exception handler is called for every exception before any
 * frame-based handler, no matter how the stack was formed.  This DLL
 * registers one that forwards access violations to the engine's own
 * CTStream::ExceptionFilter.  When the fault is a stream page, the page is
 * committed and execution resumes exactly as it would have on 32-bit Windows.
 * Every other exception passes through unchanged.
 *
 *
 * 2. "Saving is disabled in this version!"
 * ========================================
 * In this build CWorldEditorDoc::OnSaveDocument (0x00430300) is a stub. It
 * only shows that message and returns FALSE, so the real save code isn't in
 * the exe.  The engine part is still there: CWorld::Save_t is exported by
 * Engine.dll and used by other editor commands.
 *
 * Fix: this DLL supplies its own OnSaveDocument and puts it in the
 * CWorldEditorDoc vtable.  It mirrors the editor's OnOpenDocument (0x00430010):
 * make the path relative to the game folder with
 * CTFileName::RemoveApplicationPath_t(), save the document's world
 * (m_woWorld, at this+0x208) with CWorld::Save_t(), and clear the
 * modified flag.  Engine errors (C++ exceptions carrying a char * message)
 * are shown with the engine's WarningMessage(), the same way the stub showed
 * its message.
 *
 *
 * 3. Free camera (new feature)
 * ============================
 * Esc in a perspective view switches to a fly-through camera: the mouse
 * looks around, WASD moves, Q/E move down/up, Shift is faster and the mouse
 * wheel changes the speed.  Esc again (or switching to another window) goes
 * back to normal editing.
 *
 * The camera of an editor window is its CChildFrame's master viewer, which
 * all four views render from.  Free cam moves that viewer's eye placement
 * with the engine's CPlacement3D::Translate_OwnSystem(), exactly like the
 * editor's own camera commands, and repaints the views.  Esc reaches
 * CWorldEditorView::OnKeyDown (Esc isn't an accelerator), so that message
 * map entry is redirected here.  While flying, a WH_GETMESSAGE hook keeps
 * W/A/S/D/Q/E from triggering the editor's accelerators and keeps the mouse
 * away from the view's own handlers.
 *
 *
 * 4. Crash at startup when the released Serious Sam editor was used before
 * =========================================================================
 * The main window saves and restores its toolbar layout with MFC's
 * SaveBarState/LoadBarState("General") under HKCU\Software\CroTeam\WorldEditor,
 * the same registry key the released Serious Sam editor uses.  Its toolbars
 * have other IDs, and MFC's SetDockState uses GetControlBar(nBarID) without a
 * NULL check, so loading that editor's layout crashes in MFC42.DLL at
 * 5F40D84D (called from 0x0041BB5A).
 *
 * Fix: the alpha's layout goes to its own section, "Alpha toolbars", for both
 * saving and loading, so neither editor reads the other's layout.  Before
 * loading, every saved bar ID is checked against the bars that exist; an
 * unusable layout is skipped and the default toolbar layout is used.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string.h>

#pragma comment(lib, "user32.lib")

#define AFX_IDW_STATUS_BAR  0xE801   /* MFC's status bar control ID */


/* ---------------------------------------------------------------------- */
/* 1. Stream page faults                                                   */
/* ---------------------------------------------------------------------- */

/* static INDEX CTStream::ExceptionFilter(DWORD dwCode, _EXCEPTION_POINTERS *pExceptionInfoPtrs) */
typedef int (__cdecl *ExceptionFilter_t)(DWORD dwCode, EXCEPTION_POINTERS *pExceptionInfo);

static ExceptionFilter_t _pfnStreamFilter = NULL;
static PVOID             _pvHandler       = NULL;
static DWORD             _dwTlsGuard      = TLS_OUT_OF_INDEXES;

static int CallStreamFilter(DWORD dwCode, EXCEPTION_POINTERS *pExceptionInfo)
{
  /* The engine's filter assumes the calling thread has its stream list set
     up.  On any other thread it could fault itself.  Treat that as "not a
     stream fault" so the original exception keeps propagating. */
  __try {
    return _pfnStreamFilter(dwCode, pExceptionInfo);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
}

static LONG CALLBACK StreamVectoredHandler(EXCEPTION_POINTERS *pExceptionInfo)
{
  DWORD dwCode = pExceptionInfo->ExceptionRecord->ExceptionCode;
  DWORD dwLastError;
  int iResult;

  if (dwCode != EXCEPTION_ACCESS_VIOLATION || _pfnStreamFilter == NULL) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  /* Faulting code may look at GetLastError() after it resumes. Don't change it. */
  dwLastError = GetLastError();

  /* Guard against re-entry: a fault inside the filter dispatches to us again. */
  if (TlsGetValue(_dwTlsGuard) != NULL) {
    SetLastError(dwLastError);
    return EXCEPTION_CONTINUE_SEARCH;
  }
  TlsSetValue(_dwTlsGuard, (LPVOID)1);
  iResult = CallStreamFilter(dwCode, pExceptionInfo);
  TlsSetValue(_dwTlsGuard, NULL);

  SetLastError(dwLastError);
  return (iResult == EXCEPTION_CONTINUE_EXECUTION) ? EXCEPTION_CONTINUE_EXECUTION
                                                   : EXCEPTION_CONTINUE_SEARCH;
}

static void InstallStreamFix(HMODULE hEngine)
{
  _pfnStreamFilter = (ExceptionFilter_t)GetProcAddress(hEngine,
    "?ExceptionFilter@CTStream@@SAHKPAU_EXCEPTION_POINTERS@@@Z");
  if (_pfnStreamFilter == NULL) return;

  _dwTlsGuard = TlsAlloc();
  if (_dwTlsGuard == TLS_OUT_OF_INDEXES) {
    _pfnStreamFilter = NULL;
    return;
  }

  _pvHandler = AddVectoredExceptionHandler(1, StreamVectoredHandler);
}

static void UninstallStreamFix(void)
{
  if (_pvHandler != NULL) {
    RemoveVectoredExceptionHandler(_pvHandler);
    _pvHandler = NULL;
  }
  _pfnStreamFilter = NULL;
  if (_dwTlsGuard != TLS_OUT_OF_INDEXES) {
    TlsFree(_dwTlsGuard);
    _dwTlsGuard = TLS_OUT_OF_INDEXES;
  }
}


/* ---------------------------------------------------------------------- */
/* 2. Saving worlds                                                        */
/* ---------------------------------------------------------------------- */

/* WorldEditor.exe, build of 13 Jan 2000 (non-relocatable, always at 0x00400000) */
#define WE_IMAGE_BASE           0x00400000
#define WE_TIMESTAMP            0x387DDB2A
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

static CTFileName_Ctor_t                 _pfnFileNameCtor;
static CTFileName_Dtor_t                 _pfnFileNameDtor;
static CTFileName_RemoveApplicationPath_t _pfnRemoveApplicationPath;
static CWorld_Save_t                     _pfnWorldSave;
static WarningMessage_t                  _pfnWarningMessage;
static BOOL                              _bSaveFixInstalled = FALSE;

/* MSVC C++ exceptions ('throw' in Engine.dll, built with VC 6) */
#define CPP_EXCEPTION_CODE  0xE06D7363
#define CPP_EH_MAGIC_VC6    0x19930520

typedef struct { DWORD dwVFTable; DWORD dwSpare; char strName[1]; } TypeDescriptor;
typedef struct { DWORD dwProperties; TypeDescriptor *ptd; DWORD adwDisp[3]; DWORD dwSize; DWORD dwCopy; } CatchableType;
typedef struct { int ctTypes; CatchableType *apct[1]; } CatchableTypeArray;
typedef struct { DWORD dwAttributes; DWORD dwUnwind; DWORD dwForwardCompat; CatchableTypeArray *pcta; } ThrowInfo;

/* Exception filter for SaveWorld(): catch any engine C++ exception, and copy
   its message out when it is the engine's usual 'throw char *'. */
static int CatchEngineError(EXCEPTION_POINTERS *pExceptionInfo, char *strError, size_t ctError)
{
  EXCEPTION_RECORD *per = pExceptionInfo->ExceptionRecord;
  const ThrowInfo *pti;
  const char *strType;
  const char *strMessage;

  if (per->ExceptionCode != CPP_EXCEPTION_CODE || per->NumberParameters < 3
   || per->ExceptionInformation[0] != CPP_EH_MAGIC_VC6) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  lstrcpynA(strError, "Cannot save world: unknown error.", (int)ctError);
  pti = (const ThrowInfo *)per->ExceptionInformation[2];
  if (pti == NULL || pti->pcta == NULL || pti->pcta->ctTypes < 1) return EXCEPTION_EXECUTE_HANDLER;
  strType = pti->pcta->apct[0]->ptd->strName;
  if (lstrcmpA(strType, ".PAD") == 0 || lstrcmpA(strType, ".PBD") == 0) {   /* char *, const char * */
    strMessage = *(const char **)per->ExceptionInformation[1];
    if (strMessage != NULL) lstrcpynA(strError, strMessage, (int)ctError);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

static BOOL SaveWorld(void *pDoc, const char *strPathName, char *strError, size_t ctError)
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
  } __except (CatchEngineError(GetExceptionInformation(), strError, ctError)) {
    bSaved = FALSE;
  }
  if (bConstructed) _pfnFileNameDtor(&fnmWorld, NULL);
  return bSaved;
}

/* BOOL CWorldEditorDoc::OnSaveDocument(LPCTSTR lpszPathName) */
static BOOL __fastcall WorldEditorDoc_OnSaveDocument(void *pDoc, void *pvEdx, const char *strPathName)
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

static BOOL SetVTableSlot(void **ppvSlot, void *pvValue)
{
  DWORD dwOldProtect;
  if (!VirtualProtect(ppvSlot, sizeof(void *), PAGE_READWRITE, &dwOldProtect)) return FALSE;
  *ppvSlot = pvValue;
  VirtualProtect(ppvSlot, sizeof(void *), dwOldProtect, &dwOldProtect);
  return TRUE;
}

/* Only touch the exact WorldEditor build this was written for. */
static BOOL IsKnownWorldEditor(void)
{
  const BYTE *pubExe = (const BYTE *)GetModuleHandleA(NULL);
  const IMAGE_NT_HEADERS *pnt;

  if (pubExe != (const BYTE *)WE_IMAGE_BASE) return FALSE;
  pnt = (const IMAGE_NT_HEADERS *)(pubExe + ((const IMAGE_DOS_HEADER *)pubExe)->e_lfanew);
  return pnt->FileHeader.TimeDateStamp == WE_TIMESTAMP;
}

static void InstallSaveFix(HMODULE hEngine)
{
  if (!IsKnownWorldEditor()) return;
  if (WE_DOC_VTABLE[WE_VTBL_ONSAVEDOCUMENT] != WE_ONSAVEDOCUMENT_STUB) return;

  _pfnFileNameCtor          = (CTFileName_Ctor_t)GetProcAddress(hEngine, "??0CTFileName@@QAE@PBDH@Z");
  _pfnFileNameDtor          = (CTFileName_Dtor_t)GetProcAddress(hEngine, "??1CTFileName@@QAE@XZ");
  _pfnRemoveApplicationPath = (CTFileName_RemoveApplicationPath_t)GetProcAddress(hEngine, "?RemoveApplicationPath_t@CTFileName@@QAEXXZ");
  _pfnWorldSave             = (CWorld_Save_t)GetProcAddress(hEngine, "?Save_t@CWorld@@QAEXABVCTFileName@@@Z");
  _pfnWarningMessage        = (WarningMessage_t)GetProcAddress(hEngine, "?WarningMessage@@YAXPBDZZ");
  if (_pfnFileNameCtor == NULL || _pfnFileNameDtor == NULL || _pfnRemoveApplicationPath == NULL
   || _pfnWorldSave == NULL || _pfnWarningMessage == NULL) {
    return;
  }

  _bSaveFixInstalled = SetVTableSlot(&WE_DOC_VTABLE[WE_VTBL_ONSAVEDOCUMENT], (void *)WorldEditorDoc_OnSaveDocument);
}

static void UninstallSaveFix(void)
{
  if (_bSaveFixInstalled) {
    SetVTableSlot(&WE_DOC_VTABLE[WE_VTBL_ONSAVEDOCUMENT], WE_ONSAVEDOCUMENT_STUB);
    _bSaveFixInstalled = FALSE;
  }
}


/* ---------------------------------------------------------------------- */
/* 3. Free camera                                                          */
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

static Placement_TranslateOwnSystem_t _pfnTranslateOwnSystem;
static BOOL _bFreeCamInstalled = FALSE;

static struct {
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

static BOOL IsFreeCamKey(WPARAM vk)
{
  return vk == 'W' || vk == 'A' || vk == 'S' || vk == 'D' || vk == 'Q' || vk == 'E';
}

static BOOL IsKeyDown(int vk)
{
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

/* The view or the engine's "ViewPort Window" that covers it. */
static BOOL IsInView(HWND hwnd)
{
  return hwnd != NULL && (hwnd == _fc.hwndView || IsChild(_fc.hwndView, hwnd));
}

static CMasterViewer *GetMasterViewer(void *pView)
{
  char *pFrame = (char *)((Wnd_GetParentFrame_t)WE_GETPARENTFRAME)(pView, NULL);
  return (pFrame != NULL) ? (CMasterViewer *)(pFrame + WE_FRAME_MASTERVIEWER) : NULL;
}

static void UpdateAllViews(void *pView, void *pSender)
{
  void *pDoc = *(void **)((char *)pView + WE_VIEW_DOCUMENT);
  if (pDoc != NULL) ((Document_UpdateAllViews_t)WE_UPDATEALLVIEWS)(pDoc, NULL, pSender, 0, NULL);
}

static POINT GetViewCenter(HWND hwnd)
{
  RECT rc;
  POINT pt;
  GetClientRect(hwnd, &rc);
  pt.x = (rc.left + rc.right) / 2;
  pt.y = (rc.top + rc.bottom) / 2;
  ClientToScreen(hwnd, &pt);
  return pt;
}

static void ClipCursorToView(HWND hwnd)
{
  RECT rc;
  GetClientRect(hwnd, &rc);
  MapWindowPoints(hwnd, NULL, (POINT *)&rc, 2);
  ClipCursor(&rc);
}

static void ExitFreeCam(void);

static void ShowFreeCamStatus(void)
{
  char strStatus[256];
  if (_fc.hwndStatusBar == NULL || !IsWindow(_fc.hwndStatusBar)) return;
  wsprintfA(strStatus, "Free camera (%d m/s): mouse to look, WASD to move, Q/E down/up, "
    "Shift faster, wheel changes speed, Esc to exit", (int)(_fc.fBaseSpeed * _fc.fWheelFactor + 0.5f));
  SendMessageA(_fc.hwndStatusBar, SB_SETTEXTA, 0, (LPARAM)strStatus);
}

/* The editor rewrites the status text when modifier keys change (Shift...). Put ours back. */
static void KeepFreeCamStatus(void)
{
  char strCurrent[256];
  if (_fc.hwndStatusBar == NULL || !IsWindow(_fc.hwndStatusBar)) return;
  if (LOWORD(SendMessageA(_fc.hwndStatusBar, SB_GETTEXTLENGTHA, 0, 0)) >= sizeof(strCurrent)) return;
  SendMessageA(_fc.hwndStatusBar, SB_GETTEXTA, 0, (LPARAM)strCurrent);
  if (strncmp(strCurrent, "Free camera", 11) != 0) ShowFreeCamStatus();
}

/* While flying, keep the movement keys away from the editor's accelerators
   (W, S and D are editor commands) and the mouse away from the view. */
static LRESULT CALLBACK FreeCamMessageHook(int nCode, WPARAM wParam, LPARAM lParam)
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

static void CALLBACK FreeCamTick(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
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

static void EnterFreeCam(void *pView)
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

static void ExitFreeCam(void)
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
static void __fastcall WorldEditorView_OnKeyDown(void *pView, void *pvEdx, UINT nChar, UINT nRepCnt, UINT nFlags)
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

static void InstallFreeCam(HMODULE hEngine)
{
  if (!IsKnownWorldEditor()) return;
  if (*WE_VIEW_KEYDOWN_ENTRY != WE_VIEW_ONKEYDOWN) return;

  _pfnTranslateOwnSystem = (Placement_TranslateOwnSystem_t)GetProcAddress(hEngine,
    "?Translate_OwnSystem@CPlacement3D@@QAEXABV?$Vector@M$02@@@Z");
  if (_pfnTranslateOwnSystem == NULL) return;

  _bFreeCamInstalled = SetVTableSlot(WE_VIEW_KEYDOWN_ENTRY, (void *)WorldEditorView_OnKeyDown);
}

static void UninstallFreeCam(void)
{
  ExitFreeCam();
  if (_bFreeCamInstalled) {
    SetVTableSlot(WE_VIEW_KEYDOWN_ENTRY, WE_VIEW_ONKEYDOWN);
    _bFreeCamInstalled = FALSE;
  }
}


/* ---------------------------------------------------------------------- */
/* 4. Toolbar layout shared with the released editor                       */
/* ---------------------------------------------------------------------- */

/* CMainFrame keeps its toolbar layout with LoadBarState/SaveBarState("General")
   under HKCU\Software\CroTeam\WorldEditor, the same key the released Serious
   Sam editor uses.  MFC's SetDockState doesn't check for bar IDs that don't
   exist ("toolbar id's probably changed"), so a layout saved by that editor
   crashes this one in MFC42.DLL (5F40D84D) right after startup. */
#define WE_LOADBAR_PUSH         ((BYTE *)0x0041BB53)   /* push offset "General" */
#define WE_LOADBAR_CALL         ((BYTE *)0x0041BB5A)   /* call CFrameWnd::LoadBarState */
#define WE_SAVEBAR_PUSH         ((BYTE *)0x0041CBDA)   /* push offset "General" */
#define WE_LOADBARSTATE         ((void *)0x004503D8)   /* CFrameWnd::LoadBarState thunk */
#define WE_AFXGETMODULESTATE    ((void *)0x00450270)   /* AfxGetModuleState thunk */
#define WE_GETPROFILEINT        ((void *)0x004504CE)   /* CWinApp::GetProfileInt thunk */
#define WE_MODULESTATE_APP      4                      /* AFX_MODULE_STATE::m_pCurrentWinApp */
#define MAX_SAVED_BARS          256

typedef void  (__fastcall *Frame_LoadBarState_t)(void *pFrame, void *pvEdx, const char *strProfileName);
typedef void *(__cdecl *AfxGetModuleState_t)(void);
typedef UINT  (__fastcall *App_GetProfileInt_t)(void *pApp, void *pvEdx, const char *strSection, const char *strEntry, int iDefault);

/* The alpha's own section name, so neither editor reads the other's layout. */
static const char _strBarSection[] = "Alpha toolbars";

static const BYTE _aubLoadPushOld[5] = { 0x68, 0x1C, 0xCA, 0x46, 0x00 };
static const BYTE _aubLoadCallOld[5] = { 0xE8, 0x79, 0x48, 0x03, 0x00 };
static const BYTE _aubSavePushOld[5] = { 0x68, 0x24, 0xD0, 0x46, 0x00 };
static BOOL _bBarStateFixInstalled = FALSE;
static BOOL _bBarStateCodeTouched = FALSE;     /* some bytes may have been written */

static BOOL WriteCode(BYTE *pubCode, const BYTE *pubNew, size_t ctBytes)
{
  DWORD dwOldProtect;
  if (!VirtualProtect(pubCode, ctBytes, PAGE_EXECUTE_READWRITE, &dwOldProtect)) return FALSE;
  memcpy(pubCode, pubNew, ctBytes);
  VirtualProtect(pubCode, ctBytes, dwOldProtect, &dwOldProtect);
  FlushInstructionCache(GetCurrentProcess(), pubCode, ctBytes);
  return TRUE;
}

typedef struct {
  HWND hwndFrame;
  UINT nID;
  BOOL bFound;
} BarSearch;

static BOOL CALLBACK FindBarInChildren(HWND hwnd, LPARAM lParam)
{
  BarSearch *pbs = (BarSearch *)lParam;
  if ((UINT)GetDlgCtrlID(hwnd) == pbs->nID) pbs->bFound = TRUE;
  return !pbs->bFound;
}

static BOOL CALLBACK FindBarInThreadWindows(HWND hwnd, LPARAM lParam)
{
  BarSearch *pbs = (BarSearch *)lParam;
  /* the main frame, or one of its floating toolbar frames */
  if (hwnd == pbs->hwndFrame || GetWindow(hwnd, GW_OWNER) == pbs->hwndFrame) {
    EnumChildWindows(hwnd, FindBarInChildren, lParam);
  }
  return !pbs->bFound;
}

/* Same answer as CFrameWnd::GetControlBar(nID) != NULL: control bars are
   windows with the bar ID as their control ID. */
static BOOL FrameHasControlBar(HWND hwndFrame, UINT nID)
{
  BarSearch bs;
  bs.hwndFrame = hwndFrame;
  bs.nID = nID;
  bs.bFound = FALSE;
  EnumThreadWindows(GetCurrentThreadId(), FindBarInThreadWindows, (LPARAM)&bs);
  return bs.bFound;
}

/* Read the layout the way CDockState::LoadState does and check that every
   docked bar it names exists, before MFC gets to use it. */
static BOOL IsBarStateUsable(void *pFrame, const char *strSection)
{
  HWND hwndFrame = *(HWND *)((char *)pFrame + WE_VIEW_HWND);
  void *pModuleState = ((AfxGetModuleState_t)WE_AFXGETMODULESTATE)();
  void *pApp = (pModuleState != NULL) ? *(void **)((char *)pModuleState + WE_MODULESTATE_APP) : NULL;
  App_GetProfileInt_t pfnGetProfileInt = (App_GetProfileInt_t)WE_GETPROFILEINT;
  char strKey[128], strEntry[32];
  int ctBars, iBar, ctIDs, iID;

  if (pApp == NULL || !IsWindow(hwndFrame)) return FALSE;

  wsprintfA(strKey, "%s-Summary", strSection);
  ctBars = (int)pfnGetProfileInt(pApp, NULL, strKey, "Bars", 0);
  if (ctBars <= 0) return TRUE;                  /* nothing saved yet */
  if (ctBars > MAX_SAVED_BARS) return FALSE;

  for (iBar = 0; iBar < ctBars; iBar++) {
    wsprintfA(strKey, "%s-Bar%d", strSection, iBar);
    /* floating bars get a new frame from MFC; only docked ones are looked up */
    if (!pfnGetProfileInt(pApp, NULL, strKey, "Floating", FALSE)
     && !FrameHasControlBar(hwndFrame, pfnGetProfileInt(pApp, NULL, strKey, "BarID", 0))) {
      return FALSE;
    }
    /* bars listed inside a dock bar; a non-zero high word marks a placeholder */
    ctIDs = (int)pfnGetProfileInt(pApp, NULL, strKey, "Bars", 0);
    if (ctIDs < 0 || ctIDs > MAX_SAVED_BARS) return FALSE;
    for (iID = 0; iID < ctIDs; iID++) {
      UINT nID;
      wsprintfA(strEntry, "Bar#%d", iID);
      nID = pfnGetProfileInt(pApp, NULL, strKey, strEntry, 0);
      if (nID != 0 && HIWORD(nID) == 0 && !FrameHasControlBar(hwndFrame, nID)) return FALSE;
    }
  }
  return TRUE;
}

/* Replaces the CMainFrame call to LoadBarState(). An unusable layout is
   skipped, so the editor starts with its default toolbar layout instead. */
static void __fastcall MainFrame_LoadBarState(void *pFrame, void *pvEdx, const char *strSection)
{
  (void)pvEdx;
  if (IsBarStateUsable(pFrame, strSection)) {
    ((Frame_LoadBarState_t)WE_LOADBARSTATE)(pFrame, NULL, strSection);
  }
}

static void InstallBarStateFix(void)
{
  BYTE aubPush[5], aubCall[5];
  DWORD dwSection = (DWORD)(DWORD_PTR)_strBarSection;
  LONG slRelative = (LONG)((DWORD_PTR)MainFrame_LoadBarState - (DWORD_PTR)(WE_LOADBAR_CALL + 5));

  if (!IsKnownWorldEditor()) return;
  if (memcmp(WE_LOADBAR_PUSH, _aubLoadPushOld, 5) != 0
   || memcmp(WE_LOADBAR_CALL, _aubLoadCallOld, 5) != 0
   || memcmp(WE_SAVEBAR_PUSH, _aubSavePushOld, 5) != 0) {
    return;
  }

  aubPush[0] = 0x68;                             /* push offset _strBarSection */
  memcpy(aubPush + 1, &dwSection, 4);
  aubCall[0] = 0xE8;                             /* call MainFrame_LoadBarState */
  memcpy(aubCall + 1, &slRelative, 4);

  _bBarStateCodeTouched = TRUE;
  _bBarStateFixInstalled = WriteCode(WE_SAVEBAR_PUSH, aubPush, 5)
                        && WriteCode(WE_LOADBAR_PUSH, aubPush, 5)
                        && WriteCode(WE_LOADBAR_CALL, aubCall, 5);
}

static void UninstallBarStateFix(void)
{
  /* restore all three, also after a partial install */
  if (!_bBarStateCodeTouched) return;
  if (memcmp(WE_SAVEBAR_PUSH, _aubSavePushOld, 5) != 0) WriteCode(WE_SAVEBAR_PUSH, _aubSavePushOld, 5);
  if (memcmp(WE_LOADBAR_PUSH, _aubLoadPushOld, 5) != 0) WriteCode(WE_LOADBAR_PUSH, _aubLoadPushOld, 5);
  if (memcmp(WE_LOADBAR_CALL, _aubLoadCallOld, 5) != 0) WriteCode(WE_LOADBAR_CALL, _aubLoadCallOld, 5);
  _bBarStateFixInstalled = FALSE;
  _bBarStateCodeTouched = FALSE;
}


/* ---------------------------------------------------------------------- */

BOOL WINAPI DllMain(HINSTANCE hInstDll, DWORD dwReason, LPVOID pvReserved)
{
  HMODULE hEngine;

  switch (dwReason) {
  case DLL_PROCESS_ATTACH:
    DisableThreadLibraryCalls(hInstDll);
    hEngine = GetModuleHandleA("Engine.dll");
    if (hEngine != NULL) {
      InstallStreamFix(hEngine);
      InstallSaveFix(hEngine);
      InstallFreeCam(hEngine);
      InstallBarStateFix();
    }
    break;
  case DLL_PROCESS_DETACH:
    /* At process exit (pvReserved != NULL) skip cleanup: Engine.dll may already be gone. */
    if (pvReserved == NULL) {
      UninstallBarStateFix();
      UninstallFreeCam();
      UninstallSaveFix();
      UninstallStreamFix();
    } else {
      _pfnStreamFilter = NULL;
    }
    break;
  }
  return TRUE;
}
