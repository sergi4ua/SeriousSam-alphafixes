/*
 * winmm.dll proxy that loads AlphaFixes.dll into SeriousSam.exe.
 *
 * Engine.dll imports winmm.dll, and the loader looks in the game's Bin folder
 * before System32, so this DLL is loaded instead of the system one.  Every
 * export jumps straight to the real winmm.dll (same names and ordinals), so
 * the game, the editors and any system DLL that imports winmm work as before.
 *
 * In SeriousSam.exe only, the exe's entry point is redirected for one call:
 * once the loader has finished (no loader lock held), AlphaFixes.dll is
 * loaded from this folder and its AF_Init() runs, then the game starts
 * normally.  If AlphaFixes.dll is missing, the game runs unmodified.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* ---------------------------------------------------------------------- */
/* Forwarding stubs                                                        */
/* ---------------------------------------------------------------------- */

#define WINMM_EXPORT(index, stub, name, ordinal)
#include "winmm_exports.h"
#undef WINMM_EXPORT

static FARPROC _apfnReal[WINMM_EXPORT_COUNT];
static HMODULE _hRealWinmm = NULL;

#define WINMM_EXPORT(index, stub, name, ordinal) \
  __declspec(naked) void stub(void) { __asm { jmp dword ptr [_apfnReal + index * 4] } }
#include "winmm_exports.h"
#undef WINMM_EXPORT

typedef struct {
  const char *strName;     /* NULL: by ordinal */
  WORD        iOrdinal;
} ExportName;

static const ExportName _aExports[WINMM_EXPORT_COUNT] = {
#define WINMM_EXPORT(index, stub, name, ordinal) { name, ordinal },
#include "winmm_exports.h"
#undef WINMM_EXPORT
};

/* Called through a stub whose real function couldn't be found. */
static void __cdecl MissingExport(void)
{
  SetLastError(ERROR_PROC_NOT_FOUND);
}

static BOOL LoadRealWinmm(void)
{
  char strPath[MAX_PATH];
  UINT ctPath = GetSystemDirectoryA(strPath, MAX_PATH);   /* SysWOW64 for a 32-bit process */
  int i;

  if (ctPath == 0 || ctPath > MAX_PATH - 12) return FALSE;
  lstrcatA(strPath, "\\winmm.dll");
  _hRealWinmm = LoadLibraryA(strPath);
  if (_hRealWinmm == NULL) return FALSE;

  for (i = 0; i < WINMM_EXPORT_COUNT; i++) {
    const char *strProc = _aExports[i].strName != NULL ? _aExports[i].strName
                                                       : (const char *)(ULONG_PTR)_aExports[i].iOrdinal;
    _apfnReal[i] = GetProcAddress(_hRealWinmm, strProc);
    if (_apfnReal[i] == NULL) _apfnReal[i] = (FARPROC)MissingExport;
  }
  return TRUE;
}

/* ---------------------------------------------------------------------- */
/* Loading AlphaFixes.dll at the exe's entry point                     */
/* ---------------------------------------------------------------------- */

#define SERIOUSSAM_TIMESTAMP 0x387DDACA   /* SeriousSam.exe of 13 Jan 2000 */

static HMODULE _hThisDll = NULL;
static BYTE   *_pubExeEntry = NULL;
static BYTE    _aubEntryOld[5];

typedef BOOL (__cdecl *AF_Init_t)(HMODULE hProxy);

static void WriteCode(BYTE *pubCode, const BYTE *pubNew, SIZE_T ctBytes)
{
  DWORD dwOld;
  if (VirtualProtect(pubCode, ctBytes, PAGE_EXECUTE_READWRITE, &dwOld)) {
    memcpy(pubCode, pubNew, ctBytes);
    VirtualProtect(pubCode, ctBytes, dwOld, &dwOld);
    FlushInstructionCache(GetCurrentProcess(), pubCode, ctBytes);
  }
}

/* Runs at the exe's entry point, before the C runtime and WinMain. */
static void __stdcall LoadModAtEntry(void)
{
  char strPath[MAX_PATH];
  char *pchSlash;
  HMODULE hMod;
  AF_Init_t pfnInit;

  WriteCode(_pubExeEntry, _aubEntryOld, sizeof(_aubEntryOld));

  if (GetModuleFileNameA(_hThisDll, strPath, MAX_PATH) == 0) return;
  pchSlash = strrchr(strPath, '\\');
  if (pchSlash == NULL) return;
  lstrcpyA(pchSlash + 1, "AlphaFixes.dll");

  hMod = LoadLibraryA(strPath);
  if (hMod == NULL) return;
  pfnInit = (AF_Init_t)GetProcAddress(hMod, "AF_Init");
  if (pfnInit != NULL) pfnInit(_hThisDll);
}

static __declspec(naked) void EntryStub(void)
{
  __asm {
    pushad
    pushfd
    call LoadModAtEntry
    popfd
    popad
    jmp  dword ptr [_pubExeEntry]
  }
}

static BOOL IsSeriousSamExe(const BYTE *pubExe)
{
  const IMAGE_NT_HEADERS *pnt = (const IMAGE_NT_HEADERS *)(pubExe + ((const IMAGE_DOS_HEADER *)pubExe)->e_lfanew);
  return pnt->FileHeader.TimeDateStamp == SERIOUSSAM_TIMESTAMP;
}

static void HookExeEntry(void)
{
  BYTE *pubExe = (BYTE *)GetModuleHandleA(NULL);
  const IMAGE_NT_HEADERS *pnt;
  BYTE aubJump[5];
  LONG slRel;

  if (pubExe == NULL || !IsSeriousSamExe(pubExe)) return;
  pnt = (const IMAGE_NT_HEADERS *)(pubExe + ((const IMAGE_DOS_HEADER *)pubExe)->e_lfanew);
  _pubExeEntry = pubExe + pnt->OptionalHeader.AddressOfEntryPoint;

  memcpy(_aubEntryOld, _pubExeEntry, sizeof(_aubEntryOld));
  slRel = (LONG)((BYTE *)EntryStub - (_pubExeEntry + 5));
  aubJump[0] = 0xE9;                               /* jmp EntryStub */
  memcpy(aubJump + 1, &slRel, 4);
  WriteCode(_pubExeEntry, aubJump, sizeof(aubJump));
}

BOOL WINAPI DllMain(HINSTANCE hInstDll, DWORD dwReason, LPVOID pvReserved)
{
  (void)pvReserved;
  if (dwReason == DLL_PROCESS_ATTACH) {
    _hThisDll = hInstDll;
    DisableThreadLibraryCalls(hInstDll);
    if (!LoadRealWinmm()) return FALSE;
    HookExeEntry();
  }
  return TRUE;
}
