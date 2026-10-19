/*
    editorfix - fixes for Serious Sam Alpha editor
    Copyright (C) 2026 Sergi4UA

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

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
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


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

static void InstallSaveFix(HMODULE hEngine)
{
  const BYTE *pubExe = (const BYTE *)GetModuleHandleA(NULL);
  const IMAGE_NT_HEADERS *pnt;

  /* only touch the exact WorldEditor build this was written for */
  if (pubExe != (const BYTE *)WE_IMAGE_BASE) return;
  pnt = (const IMAGE_NT_HEADERS *)(pubExe + ((const IMAGE_DOS_HEADER *)pubExe)->e_lfanew);
  if (pnt->FileHeader.TimeDateStamp != WE_TIMESTAMP) return;
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
    }
    break;
  case DLL_PROCESS_DETACH:
    /* At process exit (pvReserved != NULL) skip cleanup: Engine.dll may already be gone. */
    if (pvReserved == NULL) {
      UninstallSaveFix();
      UninstallStreamFix();
    } else {
      _pfnStreamFilter = NULL;
    }
    break;
  }
  return TRUE;
}
