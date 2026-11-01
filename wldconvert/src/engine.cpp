// The alpha's own engine, loaded into the converter: it checks which model and
// texture files it can really load, and does the engine pass.
//
// The engine pass loads the converted world, initializes all entities again and
// saves the world.  The converter writes entities the way the alpha reads
// them, but only their class, placement and properties: their run-time state
// (state machine, model, light source settings, physics) is what the entity's
// own code sets up when it is initialized, which only the alpha's
// Entities.dll can do.  This is what the World Editor does when an entity is
// placed or a property is changed; CWorld::ReinitializeEntities() does it for
// all of them, and CWorld::Save_t() then writes the result just as the editor
// would.
//
// The engine finds the game folder from the running exe's path (it removes
// "Bin\" from it), so this only works when the converter runs from the alpha's
// Bin folder.
#include "convert.h"

#include <windows.h>

#include <cstdio>
#include <map>
#include <vector>

namespace {

// Engine strings: CTString is one char * allocated by the engine;
// CTFileName adds one more long.
struct EngString { char *str; };
struct EngFileName { char *str; long lReserved; };

// Engine methods are __thiscall: as __fastcall with a dummy EDX argument.
typedef char *(__cdecl *StringDuplicate_t)(const char *str);
typedef void (__cdecl *InitEngine_t)(EngString strGameID, EngFileName fnmGroup);
typedef void (__cdecl *Function_t)(void);
typedef void (__fastcall *Method_t)(void *pThis, void *pvEdx);
typedef void (__fastcall *FileNameCtor_t)(EngFileName *pfnm, void *pvEdx, const char *str, int iOffset);
typedef void (__fastcall *FileNameDtor_t)(EngFileName *pfnm, void *pvEdx);
typedef void (__fastcall *WorldFile_t)(void *pwo, void *pvEdx, const EngFileName *pfnm);
typedef void *(__fastcall *StockObtain_t)(void *pStock, void *pvEdx, const EngFileName *pfnm);
typedef void (__fastcall *StockRelease_t)(void *pStock, void *pvEdx, void *pData);
typedef int (__cdecl *ExceptionFilter_t)(DWORD dwCode, EXCEPTION_POINTERS *pep);

HMODULE _hEngine;
ExceptionFilter_t _pfnStreamFilter;
DWORD _dwTlsGuard = TLS_OUT_OF_INDEXES;

struct EngineProcs {
  StringDuplicate_t pfnStringDuplicate;
  InitEngine_t pfnInitEngine;
  Function_t pfnEnableStreamHandling;
  Method_t pfnWorldCtor;
  FileNameCtor_t pfnFileNameCtor;
  FileNameDtor_t pfnFileNameDtor;
  WorldFile_t pfnLoad, pfnSave;
  Method_t pfnReinitialize;
  StockObtain_t pfnTextureObtain, pfnModelObtain;
  StockRelease_t pfnTextureRelease, pfnModelRelease;
  void **ppTextureStock, **ppModelStock;
} _ep;

void *Proc(const char *strName)
{
  void *pv = (void *)GetProcAddress(_hEngine, strName);
  if (pv == nullptr) fprintf(stderr, "Engine.dll has no %s\n", strName);
  return pv;
}

// The engine's file streams commit their memory pages on the first access
// fault, in CTStream::ExceptionFilter (SeriousSam.exe installs it around its
// main loop).  A vectored handler sees those faults first, wherever they are.
int CallStreamFilter(DWORD dwCode, EXCEPTION_POINTERS *pep)
{
  __try {
    return _pfnStreamFilter(dwCode, pep);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
}

LONG CALLBACK StreamHandler(EXCEPTION_POINTERS *pep)
{
  if (pep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
  if (TlsGetValue(_dwTlsGuard) != nullptr) return EXCEPTION_CONTINUE_SEARCH;
  TlsSetValue(_dwTlsGuard, (LPVOID)1);
  int iResult = CallStreamFilter(pep->ExceptionRecord->ExceptionCode, pep);
  TlsSetValue(_dwTlsGuard, nullptr);
  return iResult == EXCEPTION_CONTINUE_EXECUTION ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

// The engine shows warnings and errors in message boxes, which would stop a
// command-line tool: they are collected instead (Engine.dll's import of
// MessageBoxA is redirected).
std::vector<std::string> _astrMessages;

int WINAPI EngineMessageBox(HWND, LPCSTR strText, LPCSTR strCaption, UINT)
{
  std::string str = std::string(strCaption ? strCaption : "") + ": " + (strText ? strText : "");
  for (char &ch : str) if (ch == '\n' || ch == '\r') ch = ' ';
  _astrMessages.push_back(str);
  fprintf(stderr, "  [engine] %s\n", str.c_str());
  fflush(stderr);
  return IDOK;
}

bool RedirectImport(HMODULE hmod, const char *strDll, const char *strFunction, void *pvNew)
{
  BYTE *pub = (BYTE *)hmod;
  IMAGE_NT_HEADERS *pnt = (IMAGE_NT_HEADERS *)(pub + ((IMAGE_DOS_HEADER *)pub)->e_lfanew);
  IMAGE_DATA_DIRECTORY &dd = pnt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  for (IMAGE_IMPORT_DESCRIPTOR *pid = (IMAGE_IMPORT_DESCRIPTOR *)(pub + dd.VirtualAddress); pid->Name != 0; pid++) {
    if (_stricmp((const char *)(pub + pid->Name), strDll) != 0) continue;
    IMAGE_THUNK_DATA *pthName = (IMAGE_THUNK_DATA *)(pub + pid->OriginalFirstThunk);
    IMAGE_THUNK_DATA *pthAddr = (IMAGE_THUNK_DATA *)(pub + pid->FirstThunk);
    for (; pthName->u1.AddressOfData != 0; pthName++, pthAddr++) {
      if (pthName->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
      IMAGE_IMPORT_BY_NAME *pibn = (IMAGE_IMPORT_BY_NAME *)(pub + pthName->u1.AddressOfData);
      if (strcmp((const char *)pibn->Name, strFunction) != 0) continue;
      DWORD dwOld;
      VirtualProtect(&pthAddr->u1.Function, sizeof(void *), PAGE_READWRITE, &dwOld);
      pthAddr->u1.Function = (DWORD)(uintptr_t)pvNew;
      VirtualProtect(&pthAddr->u1.Function, sizeof(void *), dwOld, &dwOld);
      return true;
    }
  }
  return false;
}

// Engine errors are C++ exceptions (Engine.dll is built with VC 6) carrying a
// char * message.
const DWORD CPP_EXCEPTION_CODE = 0xE06D7363;
const DWORD CPP_EH_MAGIC_VC6 = 0x19930520;
struct TypeDescriptor { DWORD dwVFTable; DWORD dwSpare; char strName[1]; };
struct CatchableType { DWORD dwProperties; TypeDescriptor *ptd; DWORD adwDisp[3]; DWORD dwSize; DWORD dwCopy; };
struct CatchableTypeArray { int ctTypes; CatchableType *apct[1]; };
struct ThrowInfo { DWORD dwAttributes; DWORD dwUnwind; DWORD dwForwardCompat; CatchableTypeArray *pcta; };

char _strEngineError[1024];

int CatchEngineError(EXCEPTION_POINTERS *pep)
{
  EXCEPTION_RECORD *per = pep->ExceptionRecord;
  if (per->ExceptionCode != CPP_EXCEPTION_CODE || per->NumberParameters < 3 || per->ExceptionInformation[0] != CPP_EH_MAGIC_VC6) {
    sprintf_s(_strEngineError, "exception 0x%08lX at %p", per->ExceptionCode, per->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
  }
  strcpy_s(_strEngineError, "unknown engine error");
  const ThrowInfo *pti = (const ThrowInfo *)per->ExceptionInformation[2];
  if (pti != nullptr && pti->pcta != nullptr && pti->pcta->ctTypes >= 1) {
    const char *strType = pti->pcta->apct[0]->ptd->strName;
    if (strcmp(strType, ".PAD") == 0 || strcmp(strType, ".PBD") == 0) {
      const char *strMessage = *(const char **)per->ExceptionInformation[1];
      if (strMessage != nullptr) strncpy_s(_strEngineError, strMessage, _TRUNCATE);
    }
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

// A step that doesn't finish within a few minutes is stuck: report the windows
// of this process and where the main thread is, then end the process.
HANDLE _hMainThread;
HANDLE _hStepDone;
const char *_strStep = "";
const DWORD WATCHDOG_MS = 180 * 1000;
std::vector<std::string> _astrCleanup;

BOOL CALLBACK ReportWindow(HWND hwnd, LPARAM)
{
  DWORD dwPID = 0;
  GetWindowThreadProcessId(hwnd, &dwPID);
  if (dwPID != GetCurrentProcessId() || !IsWindowVisible(hwnd)) return TRUE;
  char strTitle[256] = "", strText[1024] = "";
  GetWindowTextA(hwnd, strTitle, sizeof(strTitle));
  HWND hwndChild = nullptr;
  while ((hwndChild = FindWindowExA(hwnd, hwndChild, "Static", nullptr)) != nullptr) {
    char str[1024];
    if (GetWindowTextA(hwndChild, str, sizeof(str)) > 0) strncpy_s(strText, str, _TRUNCATE);
  }
  fprintf(stderr, "  window \"%s\": %s\n", strTitle, strText);
  return TRUE;
}

DWORD WINAPI Watchdog(LPVOID)
{
  if (WaitForSingleObject(_hStepDone, WATCHDOG_MS) == WAIT_OBJECT_0) return 0;
  fprintf(stderr, "Engine pass: %s takes too long; it seems stuck.\n", _strStep);
  EnumWindows(ReportWindow, 0);
  SuspendThread(_hMainThread);
  CONTEXT ctx;
  ctx.ContextFlags = CONTEXT_CONTROL;
  if (GetThreadContext(_hMainThread, &ctx)) {
    HMODULE hmod = nullptr;
    char strModule[MAX_PATH] = "?";
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(uintptr_t)ctx.Eip, &hmod);
    if (hmod != nullptr) GetModuleFileNameA(hmod, strModule, MAX_PATH);
    fprintf(stderr, "  main thread at %08lX (%s)\n", (unsigned long)ctx.Eip, strModule);
  }
  for (const std::string &str : _astrCleanup) DeleteFileA(str.c_str());
  fflush(stderr);
  TerminateProcess(GetCurrentProcess(), 3);
  return 0;
}

// one engine call, with its errors caught
enum Step { STEP_INIT, STEP_LOAD, STEP_REINIT, STEP_SAVE, STEP_TRYTEXTURE, STEP_TRYMODEL };

bool RunStep(Step step, void *pwo, const char *str)
{
  __try {
    switch (step) {
    case STEP_INIT: {
      _ep.pfnEnableStreamHandling();        // file streams work only on threads that enable them
      EngString strID = { _ep.pfnStringDuplicate("WorldEditor") };
      EngFileName fnmGroup = { _ep.pfnStringDuplicate(""), 0 };
      _ep.pfnInitEngine(strID, fnmGroup);   // frees both strings
      _ep.pfnWorldCtor(pwo, nullptr);
      break; }
    case STEP_LOAD: case STEP_SAVE: {
      EngFileName fnm;
      _ep.pfnFileNameCtor(&fnm, nullptr, str, 0);
      (step == STEP_LOAD ? _ep.pfnLoad : _ep.pfnSave)(pwo, nullptr, &fnm);
      _ep.pfnFileNameDtor(&fnm, nullptr);
      break; }
    case STEP_REINIT:
      _ep.pfnReinitialize(pwo, nullptr);
      break;
    case STEP_TRYTEXTURE: case STEP_TRYMODEL: {
      EngFileName fnm;
      _ep.pfnFileNameCtor(&fnm, nullptr, str, 0);
      bool bTexture = step == STEP_TRYTEXTURE;
      void *pStock = bTexture ? *_ep.ppTextureStock : *_ep.ppModelStock;
      void *pData = (bTexture ? _ep.pfnTextureObtain : _ep.pfnModelObtain)(pStock, nullptr, &fnm);
      if (pData != nullptr) (bTexture ? _ep.pfnTextureRelease : _ep.pfnModelRelease)(pStock, nullptr, pData);
      _ep.pfnFileNameDtor(&fnm, nullptr);
      if (pData == nullptr) { strcpy_s(_strEngineError, "not loaded"); return false; }
      break; }
    }
    return true;
  } __except (CatchEngineError(GetExceptionInformation())) {
    return false;
  }
}

bool RunWatched(const char *strWhat, Step step, void *pwo, const char *str)
{
  _strStep = strWhat;
  ResetEvent(_hStepDone);
  HANDLE hWatchdog = CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
  bool bOK = RunStep(step, pwo, str);
  SetEvent(_hStepDone);
  WaitForSingleObject(hWatchdog, INFINITE);
  CloseHandle(hWatchdog);
  return bOK;
}

bool _bStarted = false;
void *_pwo = nullptr;
std::map<std::string, bool> _mapUsable;

}  // namespace

bool AlphaEngine::Start(const std::string &strGameDir, std::string &strError)
{
  _hEngine = LoadLibraryExA((strGameDir + "\\Bin\\Engine.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (_hEngine == nullptr) {
    strError = "cannot load Engine.dll";
    return false;
  }
  _ep.pfnStringDuplicate = (StringDuplicate_t)Proc("?StringDuplicate@@YAPADPBD@Z");
  _ep.pfnInitEngine = (InitEngine_t)Proc("?InitEngine@@YAXVCTString@@VCTFileName@@@Z");
  _ep.pfnEnableStreamHandling = (Function_t)Proc("?EnableStreamHandling@CTStream@@SAXXZ");
  _ep.pfnWorldCtor = (Method_t)Proc("??0CWorld@@QAE@XZ");
  _ep.pfnFileNameCtor = (FileNameCtor_t)Proc("??0CTFileName@@QAE@PBDH@Z");
  _ep.pfnFileNameDtor = (FileNameDtor_t)Proc("??1CTFileName@@QAE@XZ");
  _ep.pfnLoad = (WorldFile_t)Proc("?Load_t@CWorld@@QAEXABVCTFileName@@@Z");
  _ep.pfnSave = (WorldFile_t)Proc("?Save_t@CWorld@@QAEXABVCTFileName@@@Z");
  _ep.pfnReinitialize = (Method_t)Proc("?ReinitializeEntities@CWorld@@QAEXXZ");
  _ep.pfnTextureObtain = (StockObtain_t)Proc("?Obtain_t@CStock_CTextureData@@QAEPAVCTextureData@@ABVCTFileName@@@Z");
  _ep.pfnModelObtain = (StockObtain_t)Proc("?Obtain_t@CStock_CModelData@@QAEPAVCModelData@@ABVCTFileName@@@Z");
  _ep.pfnTextureRelease = (StockRelease_t)Proc("?Release@CStock_CTextureData@@QAEXPAVCTextureData@@@Z");
  _ep.pfnModelRelease = (StockRelease_t)Proc("?Release@CStock_CModelData@@QAEXPAVCModelData@@@Z");
  _ep.ppTextureStock = (void **)Proc("?_pTextureStock@@3PAVCStock_CTextureData@@A");
  _ep.ppModelStock = (void **)Proc("?_pModelStock@@3PAVCStock_CModelData@@A");
  _pfnStreamFilter = (ExceptionFilter_t)Proc("?ExceptionFilter@CTStream@@SAHKPAU_EXCEPTION_POINTERS@@@Z");
  void **ppv = (void **)&_ep;
  for (size_t i = 0; i < sizeof(_ep) / sizeof(void *); i++) {
    if (ppv[i] == nullptr) {
      strError = "this Engine.dll doesn't have the expected functions";
      return false;
    }
  }
  if (_pfnStreamFilter == nullptr) {
    strError = "this Engine.dll doesn't have the expected functions";
    return false;
  }
  RedirectImport(_hEngine, "USER32.dll", "MessageBoxA", (void *)EngineMessageBox);
  _dwTlsGuard = TlsAlloc();
  AddVectoredExceptionHandler(1, StreamHandler);
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &_hMainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
  _hStepDone = CreateEventA(nullptr, TRUE, FALSE, nullptr);

  // CWorld is about 2 KB in this engine; a generous block that is never freed
  // (the process ends right after the engine pass)
  static std::vector<uint8_t> abWorld(256 * 1024);
  _pwo = abWorld.data();

  printf("Starting the alpha's engine...\n");
  fflush(stdout);
  if (!RunWatched("starting the engine", STEP_INIT, _pwo, nullptr)) {
    strError = std::string("starting the engine failed: ") + _strEngineError;
    return false;
  }
  _bStarted = true;
  return true;
}

bool AlphaEngine::CanLoad(const std::string &strRelFile)
{
  std::string strKey = ToLower(strRelFile);
  auto it = _mapUsable.find(strKey);
  if (it != _mapUsable.end()) return it->second;
  std::string strExt = strKey.size() > 4 ? strKey.substr(strKey.size() - 4) : "";
  Step step = strExt == ".tex" ? STEP_TRYTEXTURE : STEP_TRYMODEL;
  size_t ctMessages = _astrMessages.size();
  bool bOK = RunWatched(("loading " + strRelFile).c_str(), step, _pwo, strRelFile.c_str());
  // the engine reports some file errors in a message box and goes on
  if (_astrMessages.size() != ctMessages) bOK = false;
  _mapUsable[strKey] = bOK;
  return bOK;
}

void AlphaEngine::CleanUpOnAbort(const std::string &strFile)
{
  _astrCleanup.push_back(strFile);
}

bool AlphaEngine::Reinitialize(const std::string &strRelIn, const std::string &strRelOut, std::string &strError)
{
  if (!_bStarted) {
    strError = "the engine wasn't started";
    return false;
  }
  struct { const char *strWhat; Step step; const char *str; } asteps[] = {
    { "loading the converted world", STEP_LOAD, strRelIn.c_str() },
    { "initializing entities", STEP_REINIT, nullptr },
    { "saving", STEP_SAVE, strRelOut.c_str() },
  };
  for (const auto &s : asteps) {
    printf("Engine pass: %s...\n", s.strWhat);
    fflush(stdout);
    if (!RunWatched(s.strWhat, s.step, _pwo, s.str)) {
      strError = std::string(s.strWhat) + " failed: " + _strEngineError;
      return false;
    }
  }
  return true;
}

const std::vector<std::string> &AlphaEngine::Messages()
{
  return _astrMessages;
}
