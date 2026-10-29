// AlphaFixes.dll entry: loaded by the winmm.dll proxy at SeriousSam.exe's
// entry point, before WinMain.  Engine.dll, Game.dll and the exe are loaded but
// nothing of the game has run yet.
#include "alphafixes.h"

namespace {

constexpr DWORD EXE_TIMESTAMP = 0x387DDACA;      // SeriousSam.exe, 13 Jan 2000
constexpr DWORD ENGINE_TIMESTAMP = 0x387DDB80;   // Engine.dll, same build

DWORD ModuleTimestamp(HMODULE hModule)
{
  if (hModule == nullptr) return 0;
  auto *pnt = (IMAGE_NT_HEADERS *)((BYTE *)hModule + ((IMAGE_DOS_HEADER *)hModule)->e_lfanew);
  return pnt->FileHeader.TimeDateStamp;
}

constexpr DWORD ENTITIES_TIMESTAMP = 0x387DDB83; // Entities.dll, same build
constexpr uintptr_t ENTITIES_BASE = 0x601A0000;  // its preferred base; it can be relocated

bool _bInitDone = false;
bool _bActive = false;                           // known build, fixes installed
bool _bLateHooksDone = false;
uintptr_t _ulEntitiesBase = 0;

}  // namespace

uintptr_t RebaseEntities(uintptr_t ulAddress) { return ulAddress - ENTITIES_BASE + _ulEntitiesBase; }

// Entities.dll is loaded by the engine when the first world loads.  Called
// every frame and from the view hooks until it is there.
void UpdateLateHooks()
{
  if (_bLateHooksDone || !_bActive) return;
  HMODULE hEntities = GetModuleHandleA("Entities.dll");
  if (hEntities == nullptr) return;
  _bLateHooksDone = true;
  if (ModuleTimestamp(hEntities) != ENTITIES_TIMESTAMP) {
    Log("Entities.dll is not the 13 Jan 2000 build; fixes for it are off");
    return;
  }
  _ulEntitiesBase = (uintptr_t)hEntities;
  Log("Entities.dll at %08X", (unsigned)_ulEntitiesBase);
  InstallWidescreenEntitiesHooks(hEntities);
  InstallMouseEntitiesHooks(hEntities);
  InstallLightFixEntitiesHooks(hEntities);
  InstallCannonEntitiesPatches();
  InstallWeaponsEntitiesHooks();
}

extern "C" __declspec(dllexport) BOOL __cdecl AF_Init(HMODULE hProxy)
{
  (void)hProxy;
  if (_bInitDone) return TRUE;
  _bInitDone = true;

  LogOpen();
  Log(AF_TITLE " " AF_VERSION " (" __DATE__ ")%s", AF_TEST_AIDS ? ", test build with testing aids" : "");
  DWORD dwExe = ModuleTimestamp(GetModuleHandleA(nullptr));
  DWORD dwEngine = ModuleTimestamp(GetModuleHandleA("Engine.dll"));
  if (dwExe != EXE_TIMESTAMP || dwEngine != ENGINE_TIMESTAMP) {
    Log("Unknown game build (exe %08lX, engine %08lX); nothing changed", dwExe, dwEngine);
    return FALSE;
  }

  _bActive = true;
  LoadConfig();
  ApplyDpiAwareness();
  InstallStreamFix();
  InstallLightFix();
  InstallTextFixes();
  InstallGameSettingsFile();
  InstallGraphicsOptions();
  Log("In-game menu: %s", InstallInGameMenu() ? "ok" : "failed");
#if AF_TEST_AIDS
  InstallTestAids();
#endif
  Log("Display fixes: %s", InstallDisplayFixes() ? "ok" : "partly failed");
  Log("Widescreen fixes: %s", InstallWidescreenFixes() ? "ok" : "partly failed");
  Log("Mouse fixes: %s", InstallMouseFixes() ? "ok" : "partly failed");
  Log("Overlay: %s", InstallOverlay() ? "ok" : "partly failed");
  return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE hInstDll, DWORD dwReason, LPVOID)
{
  if (dwReason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(hInstDll);
  return TRUE;
}
