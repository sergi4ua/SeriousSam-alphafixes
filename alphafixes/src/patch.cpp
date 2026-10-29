// Code patching helpers.
#include "alphafixes.h"

#include "MinHook.h"

#include <cstring>

std::string GameDir()
{
  char str[MAX_PATH];
  DWORD ct = GetModuleFileNameA(nullptr, str, MAX_PATH);
  std::string strDir(str, ct);
  size_t iSlash = strDir.find_last_of('\\');
  return iSlash == std::string::npos ? std::string() : strDir.substr(0, iSlash + 1);
}

bool WriteCode(void *pDest, const void *pSrc, size_t ctBytes)
{
  DWORD dwOld;
  if (!VirtualProtect(pDest, ctBytes, PAGE_EXECUTE_READWRITE, &dwOld)) return false;
  memcpy(pDest, pSrc, ctBytes);
  VirtualProtect(pDest, ctBytes, dwOld, &dwOld);
  FlushInstructionCache(GetCurrentProcess(), pDest, ctBytes);
  return true;
}

bool CodeMatches(uintptr_t addr, const void *pExpected, size_t ctBytes)
{
  __try {
    return memcmp((const void *)addr, pExpected, ctBytes) == 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool HookFunction(void *pTarget, void *pDetour, void **ppOriginal, const char *strName)
{
  static bool _bInit = false;
  if (!_bInit) {
    if (MH_Initialize() != MH_OK) {
      Log("MinHook init failed");
      return false;
    }
    _bInit = true;
  }
  if (pTarget == nullptr) {
    Log("Hook %s: target not found", strName);
    return false;
  }
  MH_STATUS st = MH_CreateHook(pTarget, pDetour, ppOriginal);
  if (st == MH_OK) st = MH_EnableHook(pTarget);
  if (st != MH_OK) {
    Log("Hook %s at %p failed: %s", strName, pTarget, MH_StatusToString(st));
    return false;
  }
  return true;
}

void *HookImport(HMODULE hModule, const char *strDll, const char *strFunction, void *pNew)
{
  BYTE *pubBase = (BYTE *)hModule;
  if (pubBase == nullptr) return nullptr;
  auto *pnt = (IMAGE_NT_HEADERS *)(pubBase + ((IMAGE_DOS_HEADER *)pubBase)->e_lfanew);
  const IMAGE_DATA_DIRECTORY &dir = pnt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (dir.Size == 0) return nullptr;

  for (auto *pid = (IMAGE_IMPORT_DESCRIPTOR *)(pubBase + dir.VirtualAddress); pid->Name != 0; pid++) {
    if (_stricmp((const char *)(pubBase + pid->Name), strDll) != 0) continue;
    if (pid->OriginalFirstThunk == 0) continue;
    auto *pthNames = (IMAGE_THUNK_DATA *)(pubBase + pid->OriginalFirstThunk);
    auto *pthAddrs = (IMAGE_THUNK_DATA *)(pubBase + pid->FirstThunk);
    for (; pthNames->u1.AddressOfData != 0; pthNames++, pthAddrs++) {
      if (IMAGE_SNAP_BY_ORDINAL(pthNames->u1.Ordinal)) continue;
      auto *pibn = (IMAGE_IMPORT_BY_NAME *)(pubBase + pthNames->u1.AddressOfData);
      if (strcmp((const char *)pibn->Name, strFunction) != 0) continue;
      void *pOld = (void *)pthAddrs->u1.Function;
      DWORD_PTR dwNew = (DWORD_PTR)pNew;
      if (!WriteCode(&pthAddrs->u1.Function, &dwNew, sizeof(dwNew))) return nullptr;
      return pOld;
    }
  }
  Log("Import %s!%s not found", strDll, strFunction);
  return nullptr;
}

void *EngineProc(const char *strMangledName)
{
  void *p = (void *)GetProcAddress(GetModuleHandleA("Engine.dll"), strMangledName);
  if (p == nullptr) Log("Engine.dll export missing: %s", strMangledName);
  return p;
}
