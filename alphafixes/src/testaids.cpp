// Testing aids.
//
// Level travel: an empty-ish file Bin\AlphaFixes.travel asks to trigger a
// world link (CWorldLink) of the current level, as if the player had reached
// it.  The file holds part of the link's target world file name, for example
// "KingValley"; the first link whose target contains it is triggered.  With
// "@" in front ("@KingValley") the player is moved onto the link instead, so
// the game triggers it the way it does when the player walks into it.  Every
// link of the level is written to the log.  Used to test going between levels
// without playing through them.
//
// Light crashes (see lightfix.cpp): a file Bin\AlphaFixes.badlight gives
// the light behind the first lens flare on screen the invalid animation
// pointer of the first crash report.  Bin\AlphaFixes.unsetlight gives the
// player's light the state of the second report the next time it is drawn
// before it is set up, just after a level change.  Bin\AlphaFixes.lights
// writes every entity's light to the log.
//
// Menu: Bin\AlphaFixes.menudump writes every 3D menu item to the log
// (model, textures, placements, colours, label, state).
//
// Console: Bin\AlphaFixes.console holds a shell command that is run as if
// typed in the console, for example "plrCheat_bGiveAll = 1;".
//
// Video mode: Bin\AlphaFixes.videomode holding "1600x900" switches to that
// size in fullscreen ("1600x900 window" for a window), the way the settings
// window does.
//
// Console log: started with AF_LOG_CONSOLE=1, every line printed to the
// game's console is also written to the log.
//
// Game time: Bin\AlphaFixes.time writes the game's time and whether the game
// is paused to the log, after the file's text (a label for the log).
#include "alphafixes.h"

#if AF_TEST_AIDS

namespace {

// Engine.dll layout (alpha build)
constexpr size_t NET_WORLD = 0x944;                 // CNetworkLibrary::ga_World
constexpr size_t WORLD_ENTITIES = 0x100;            // CWorld::wo_cenEntities: array, count
// CWorldLink (Entities.dll)
constexpr size_t WL_WORLD = 0xC0;                   // m_strWorld (CTString = char *)
constexpr size_t WL_TYPE = 0xC4;                    // m_EwltType
constexpr int VTBL_GETNAME = 13;                    // CEntity::GetName

using IsOfClass_t = BOOL(__cdecl *)(void *pen, const char *strClass);
using SendEvent_t = void(__fastcall *)(void *pen, void *pvEdx, const void *pee);
using EventCtor_t = void(__fastcall *)(void *pee, void *pvEdx);
using GetName_t = const char *const *(__fastcall *)(void *pen, void *pvEdx);
using Teleport_t = void(__fastcall *)(void *pen, void *pvEdx, const float *ppl, BOOL bTelefrag);
constexpr size_t EN_PLACEMENT = 0x1C;               // CEntity::en_plPlacement
constexpr int VTBL_GETLIGHTSOURCE = 20;             // CEntity::GetLightSource
using GetLightSource_t = char *(__fastcall *)(void *pen, void *pvEdx);

bool ReadRequest(const char *strFile, std::string &strRequest)
{
  std::string strPath = GameDir() + strFile;
  HANDLE hFile = CreateFileA(strPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) return false;
  char str[256] = {};
  DWORD ctRead = 0;
  ReadFile(hFile, str, sizeof(str) - 1, &ctRead, nullptr);
  CloseHandle(hFile);
  DeleteFileA(strPath.c_str());
  strRequest.assign(str, ctRead);
  while (!strRequest.empty() && (strRequest.back() == '\r' || strRequest.back() == '\n' || strRequest.back() == ' ')) {
    strRequest.pop_back();
  }
  return true;
}

void TriggerWorldLink(std::string strTarget)
{
  bool bWalk = !strTarget.empty() && strTarget[0] == '@';
  if (bWalk) strTarget.erase(0, 1);
  auto pfnTeleport = (Teleport_t)EngineProc("?Teleport@CEntity@@QAEXABVCPlacement3D@@H@Z");
  auto pfnIsOfClass = (IsOfClass_t)EngineProc("?IsOfClass@@YAHPAVCEntity@@PBD@Z");
  auto pfnSendEvent = (SendEvent_t)EngineProc("?SendEvent@CEntity@@QAEXABVCEntityEvent@@@Z");
  auto ppNetwork = (char **)EngineProc("?_pNetwork@@3PAVCNetworkLibrary@@A");
  HMODULE hEntities = GetModuleHandleA("Entities.dll");
  auto pfnTriggerCtor = (EventCtor_t)(hEntities ? GetProcAddress(hEntities, "??0ETrigger@@QAE@XZ") : nullptr);
  if (!pfnIsOfClass || !pfnSendEvent || !ppNetwork || !*ppNetwork || !pfnTriggerCtor) {
    Log("Travel: no world loaded");
    return;
  }

  char *pWorld = *ppNetwork + NET_WORLD;
  void **apen = *(void ***)(pWorld + WORLD_ENTITIES);
  int ctEntities = *(int *)(pWorld + WORLD_ENTITIES + 4);
  void *penChosen = nullptr;
  void *penPlayer = nullptr;
  for (int i = 0; i < ctEntities; i++) {
    void *pen = apen[i];
    if (pen != nullptr && penPlayer == nullptr && pfnIsOfClass(pen, "Player")) penPlayer = pen;
    if (pen == nullptr || !pfnIsOfClass(pen, "World link")) continue;
    const char *strWorld = *(const char **)((char *)pen + WL_WORLD);
    const char *const *pstrName = ((GetName_t)(*(void ***)pen)[VTBL_GETNAME])(pen, nullptr);
    Log("Travel: world link '%s' -> %s (type %d)", pstrName && *pstrName ? *pstrName : "", strWorld ? strWorld : "",
        *(int *)((char *)pen + WL_TYPE));
    if (penChosen == nullptr && strWorld != nullptr && !strTarget.empty() && strstr(strWorld, strTarget.c_str()) != nullptr) {
      penChosen = pen;
    }
  }
  if (penChosen == nullptr) {
    Log("Travel: no world link to '%s'", strTarget.c_str());
    return;
  }
  if (bWalk) {
    if (penPlayer == nullptr || pfnTeleport == nullptr) {
      Log("Travel: no player to move");
      return;
    }
    pfnTeleport(penPlayer, nullptr, (const float *)((char *)penChosen + EN_PLACEMENT), FALSE);
    Log("Travel: moved the player onto the link to %s", *(const char **)((char *)penChosen + WL_WORLD));
    return;
  }
  alignas(8) char achEvent[0x40] = {};
  pfnTriggerCtor(achEvent, nullptr);    // ETrigger; penCaused stays NULL, so no destructor is needed
  pfnSendEvent(penChosen, nullptr, achEvent);
  Log("Travel: triggered the link to %s", *(const char **)((char *)penChosen + WL_WORLD));
}

// Logs every entity's light source (CEntity::GetLightSource): who returns it
// and whether it was ever set up (CLightSource's constructor leaves the flags
// at -1 and the lens flare and animation pointers unset).
void LogLights()
{
  auto ppNetwork = (char **)EngineProc("?_pNetwork@@3PAVCNetworkLibrary@@A");
  HMODULE hEntities = GetModuleHandleA("Entities.dll");
  if (!ppNetwork || !*ppNetwork || !hEntities) return;
  char *pWorld = *ppNetwork + NET_WORLD;
  void **apen = *(void ***)(pWorld + WORLD_ENTITIES);
  int ctEntities = *(int *)(pWorld + WORLD_ENTITIES + 4);
  int ctLights = 0;
  for (int i = 0; i < ctEntities; i++) {
    void *pen = apen[i];
    if (pen == nullptr) continue;
    void *pfn = (*(void ***)pen)[VTBL_GETLIGHTSOURCE];
    char *pls = ((GetLightSource_t)pfn)(pen, nullptr);
    if (pls == nullptr) continue;
    ctLights++;
    ULONG ulFlags = *(ULONG *)(pls + 0x10);
    if (ulFlags == 0xFFFFFFFF || ctLights <= 400) {
      Log("Lights: entity %p GetLightSource Entities+%X light %p flags %08X entity %p flare %p animation %p%s", pen,
          (unsigned)((char *)pfn - (char *)hEntities), (void *)pls, ulFlags, *(void **)(pls + 0xC), *(void **)(pls + 0x30),
          *(void **)(pls + 0x34), ulFlags == 0xFFFFFFFF ? "  NEVER SET UP" : "");
    }
  }
  Log("Lights: %d entities, %d with a light", ctEntities, ctLights);
}

// Menu items (SeriousSam.exe, alpha build): every 3D menu item on the menu's
// item list, with what it is made of and where it moves.  For documenting the
// alpha menu.
constexpr uintptr_t EXE_MENUITEMS = 0x43D2F0;        // CListHead of items; a node is the item + 0x10

// Copies a C string that may not be readable.
void SafeString(const void *pv, char *str, size_t ctMax)
{
  str[0] = 0;
  __try {
    const char *pch = (const char *)pv;
    size_t i = 0;
    for (; pch != nullptr && i + 1 < ctMax && pch[i] != 0; i++) str[i] = (pch[i] >= 32 && pch[i] < 127) ? pch[i] : '?';
    str[i] = 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    strcpy(str, "<unreadable>");
  }
}

// The file name of a CSerial (model or texture data): CSerial::ser_FileName (+0x0C).
void SerialName(const void *pser, char *str, size_t ctMax)
{
  str[0] = 0;
  if (pser == nullptr) return;
  const char *pchName = nullptr;
  __try {
    pchName = *(const char *const *)((const char *)pser + 0xC);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return;
  }
  SafeString(pchName, str, ctMax);
}

void LogMenuItems()
{
  using GetData_t = void *(__fastcall *)(void *pao, void *pvEdx);
  auto pfnGetData = (GetData_t)EngineProc("?GetData@CAnimObject@@QAEPAVCAnimData@@XZ");
  int ctItems = 0;
  for (char *pln = *(char **)EXE_MENUITEMS; pln != nullptr && *(char **)pln != nullptr; pln = *(char **)pln) {
    char *pmg = pln - 0x10;
    const float *af = (const float *)pmg;
    void *pmo = *(void **)(pmg + 0x7C);
    char strModel[96], strTexture[96], strLabel[64], strNext[64];
    SerialName(pmo != nullptr && pfnGetData != nullptr ? pfnGetData(pmo, nullptr) : nullptr, strModel, sizeof(strModel));
    bool bLabels = *(uintptr_t *)pmg != 0x426730;   // the base class has no label fields
    SerialName(bLabels ? *(void **)(pmg + 0xAC) : nullptr, strTexture, sizeof(strTexture));
    SafeString(bLabels ? *(void **)(pmg + 0xA0) : nullptr, strLabel, sizeof(strLabel));
    SafeString(bLabels ? *(void **)(pmg + 0xA4) : nullptr, strNext, sizeof(strNext));
    Log("Menu item %p vtbl %08X visible %d state %d target %d | %s | %s", (void *)pmg, *(unsigned *)pmg,
        *(int *)(pmg + 0x18), *(int *)(pmg + 0x98), *(int *)(pmg + 0x9C), strModel, strTexture);
    Log("  spin %g %g %g | on screen %g %g %g / %g %g %g | start %g %g %g / %g %g %g | now %g %g %g / %g %g %g",
        af[1], af[2], af[3], af[7], af[8], af[9], af[10], af[11], af[12], af[13], af[14], af[15], af[16], af[17], af[18],
        af[19], af[20], af[21], af[22], af[23], af[24]);
    Log("  light %08X -> %08X (%g) ambient %08X -> %08X (%g)", *(unsigned *)(pmg + 0x84), *(unsigned *)(pmg + 0x80),
        af[0x94 / 4], *(unsigned *)(pmg + 0x8C), *(unsigned *)(pmg + 0x88), af[0x90 / 4]);
    if (bLabels) {
      Log("  label '%s' next '%s' at %d,%d color %08X font %08X delay %g", strLabel, strNext, *(int *)(pmg + 0xC4),
          *(int *)(pmg + 0xC8), *(unsigned *)(pmg + 0xCC), *(unsigned *)(pmg + 0xD4), af[0xE0 / 4]);
    }
    ctItems++;
  }
  Log("Menu items: %d", ctItems);
}

void RunConsoleCommand(const std::string &strCommand)
{
  using Execute_t = void(__fastcall *)(void *psh, void *pvEdx, const char *const *pstrCommand);
  auto ppShell = (void **)EngineProc("?_pShell@@3PAVCShell@@A");
  auto pfnExecute = (Execute_t)EngineProc("?Execute@CShell@@QAEXABVCTString@@@Z");
  if (ppShell == nullptr || *ppShell == nullptr || pfnExecute == nullptr) return;
  const char *str = strCommand.c_str();      // a CTString is just its char pointer
  pfnExecute(*ppShell, nullptr, &str);
  Log("Console: %s", str);
}

// Engine.dll (alpha build): CConsole::PutString(const char *), behind CPrintF and CPutString
constexpr uintptr_t ENG_CONSOLE_PUTSTRING = 0x60004C50;
using PutString_t = void(__fastcall *)(void *pcon, void *pvEdx, const char *str);
PutString_t _pfnPutString = nullptr;

void __fastcall PutString_Hook(void *pcon, void *pvEdx, const char *str)
{
  std::string strLine = str ? str : "";
  while (!strLine.empty() && (strLine.back() == '\n' || strLine.back() == '\r')) strLine.pop_back();
  Log("Console> %s", strLine.c_str());
  _pfnPutString(pcon, pvEdx, str);
}

void LogGameTime(const std::string &strLabel)
{
  using CurrentTick_t = float(__fastcall *)(void *ptm, void *pvEdx);
  using IsPaused_t = BOOL(__fastcall *)(void *pnet, void *pvEdx);
  auto ppTimer = (void **)EngineProc("?_pTimer@@3PAVCTimer@@A");
  auto pfnCurrentTick = (CurrentTick_t)EngineProc("?CurrentTick@CTimer@@QBE?BMXZ");
  auto ppNetwork = (void **)EngineProc("?_pNetwork@@3PAVCNetworkLibrary@@A");
  auto pfnIsPaused = (IsPaused_t)EngineProc("?IsPaused@CNetworkLibrary@@QAEHXZ");
  if (ppTimer == nullptr || *ppTimer == nullptr || pfnCurrentTick == nullptr || ppNetwork == nullptr
      || *ppNetwork == nullptr || pfnIsPaused == nullptr) return;
  Log("Test: %s: game time %.2f s, %s", strLabel.c_str(), pfnCurrentTick(*ppTimer, nullptr),
      pfnIsPaused(*ppNetwork, nullptr) ? "paused" : "running");
}

void SwitchVideoMode(const std::string &strMode)
{
  VideoMode vm = GetCurrentVideoMode();
  if (sscanf(strMode.c_str(), "%dx%d", &vm.iWidth, &vm.iHeight) != 2) return;
  vm.bFullscreen = strMode.find("window") == std::string::npos;
  Log("Test: video mode %s", strMode.c_str());
  RequestVideoMode(vm);
}

}  // namespace

void InstallTestAids()
{
  char strEnv[8] = {};
  if (GetEnvironmentVariableA("AF_LOG_CONSOLE", strEnv, sizeof(strEnv)) > 0 && strEnv[0] == '1') {
    HookFunction((void *)ENG_CONSOLE_PUTSTRING, (void *)PutString_Hook, (void **)&_pfnPutString, "CConsole::PutString");
  }
}

// Main thread, between frames.
void RunTestAids()
{
  static DWORD _dwLastCheck = 0;
  DWORD dwNow = GetTickCount();
  if (dwNow - _dwLastCheck < 250) return;
  _dwLastCheck = dwNow;
  std::string strRequest;
  if (ReadRequest("AlphaFixes.travel", strRequest)) TriggerWorldLink(strRequest);
  if (ReadRequest("AlphaFixes.lights", strRequest)) LogLights();
  if (ReadRequest("AlphaFixes.menudump", strRequest)) LogMenuItems();
  if (ReadRequest("AlphaFixes.console", strRequest)) RunConsoleCommand(strRequest);
  if (ReadRequest("AlphaFixes.videomode", strRequest)) SwitchVideoMode(strRequest);
  if (ReadRequest("AlphaFixes.time", strRequest)) LogGameTime(strRequest);
  if (ReadRequest("AlphaFixes.unsetlight", strRequest)) {
    PoisonUnsetPlayerLight();
    Log("Bad light: armed for the player's light after the next level change");
  }
  if (ReadRequest("AlphaFixes.badlight", strRequest)) {
    Log("Bad light: %s", BreakLensFlareLight() ? "light behind the first lens flare broken" : "no lens flare on screen");
  }
}

#endif  // AF_TEST_AIDS
