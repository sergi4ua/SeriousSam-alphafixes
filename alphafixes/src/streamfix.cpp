// File streams on 64-bit Windows.
//
// The alpha engine reads files through "virtual memory streams": each file gets
// a reserved address range and pages are committed when first touched.  The
// first touch raises an access violation that CTStream::ExceptionFilter()
// handles (commit the page, read it, continue).  SeriousSam.exe installs that
// filter with __try/__except around its main loop.  On 64-bit Windows a 32-bit
// exception can't travel back through a window procedure that Windows called
// (a kernel callback), so a stream touched inside a window message never
// reaches the filter and the game crashes.  The same problem crashes the alpha
// World Editor on startup (see AlphaEditor\README.md).
//
// A vectored exception handler sees every exception first, whatever the stack
// looks like.  It hands access violations to the engine's own filter, which
// resolves stream faults exactly as on 32-bit Windows and ignores all others.
#include "alphafixes.h"

namespace {

using ExceptionFilter_t = int(__cdecl *)(DWORD dwCode, EXCEPTION_POINTERS *pExceptionInfo);
ExceptionFilter_t _pfnStreamFilter = nullptr;
DWORD _dwTlsGuard = TLS_OUT_OF_INDEXES;

int CallStreamFilter(DWORD dwCode, EXCEPTION_POINTERS *pExceptionInfo)
{
  // The filter expects the stream list of the calling thread; on other threads
  // it can fault itself.  That just means "not a stream fault".
  __try {
    return _pfnStreamFilter(dwCode, pExceptionInfo);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
}

LONG CALLBACK StreamVectoredHandler(EXCEPTION_POINTERS *pExceptionInfo)
{
  DWORD dwCode = pExceptionInfo->ExceptionRecord->ExceptionCode;
  if (dwCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;

  DWORD dwLastError = GetLastError();
  if (TlsGetValue(_dwTlsGuard) != nullptr) {   // a fault inside the filter
    SetLastError(dwLastError);
    return EXCEPTION_CONTINUE_SEARCH;
  }
  TlsSetValue(_dwTlsGuard, (LPVOID)1);
  int iResult = CallStreamFilter(dwCode, pExceptionInfo);
  TlsSetValue(_dwTlsGuard, nullptr);
  SetLastError(dwLastError);
  return iResult == EXCEPTION_CONTINUE_EXECUTION ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void InstallStreamFix()
{
  if (!g_cfg.bStreamFix) return;
  _pfnStreamFilter = (ExceptionFilter_t)EngineProc("?ExceptionFilter@CTStream@@SAHKPAU_EXCEPTION_POINTERS@@@Z");
  if (_pfnStreamFilter == nullptr) return;
  _dwTlsGuard = TlsAlloc();
  if (_dwTlsGuard == TLS_OUT_OF_INDEXES) return;
  bool bOk = AddVectoredExceptionHandler(1, StreamVectoredHandler) != nullptr;
  Log("Stream fix: %s", bOk ? "installed" : "failed");
}
