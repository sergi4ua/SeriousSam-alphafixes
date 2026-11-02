// AlphaFixesEntities.dll: new entity classes for the Serious Sam alpha.
//
// The engine loads this DLL when a world or the editor needs one of its
// classes: each class has a Classes\<name>.ecl that names this DLL as its
// package.  Entities.dll comes in as an import, so the alpha's CEnemyBase is
// there by the time DllMain runs.
#include "AlphaFixesEntities.h"

DWORD ModuleTimestamp(HMODULE hModule)
{
  const BYTE *pub = (const BYTE *)hModule;
  const IMAGE_DOS_HEADER *pdos = (const IMAGE_DOS_HEADER *)pub;
  const IMAGE_NT_HEADERS *pnt = (const IMAGE_NT_HEADERS *)(pub + pdos->e_lfanew);
  return pnt->FileHeader.TimeDateStamp;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD dwReason, LPVOID)
{
  if (dwReason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(hinstDLL);
    char strError[256];
    if (!InitEnemyBase(strError, sizeof(strError))) {
      char strMessage[400];
      snprintf(strMessage, sizeof(strMessage),
        "AlphaFixesEntities.dll can't be used: %s.\n\nIts classes won't load.", strError);
      MessageBoxA(nullptr, strMessage, "AlphaFixesEntities", MB_OK | MB_ICONERROR);
      return FALSE;
    }
  }
  return TRUE;
}
