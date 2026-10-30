// AlphaFixes - fixes for the Serious Sam alpha (SeriousSam.exe, build of 13 Jan 2000).
// Shared declarations of the mod's modules.
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

#define AF_TITLE "AlphaFixes"
#define AF_VERSION "0.6-pt1"
#define AF_AUTHOR "Sergi4UA"

// Testing aids are built only with the CMake option AF_TEST_AIDS (build.bat test).
#ifndef AF_TEST_AIDS
#define AF_TEST_AIDS 0
#endif

// ---------------------------------------------------------------------------
// Configuration (AlphaFixes.ini next to SeriousSam.exe)
// ---------------------------------------------------------------------------

enum class FullscreenMode : int {
  Exclusive = 0,   // change the display mode, like the original game
  Borderless = 1,  // keep the desktop mode and cover the screen with the game window
};

struct Config {
  // [Overlay]
  int   iToggleKey = VK_F11;           // virtual key that opens the settings window
  bool  bShowFps = false;

  // [Menu]
  bool  bInGameMenu = true;            // AlphaFixes page in the game's Options menu (see ingamemenu.cpp)

  // [Display]
  bool  bDpiAware = true;              // stop Windows from scaling the game window
  int   iRefreshRate = 0;              // fullscreen refresh rate in Hz, 0 = highest available
  FullscreenMode eFullscreenMode = FullscreenMode::Exclusive;
  bool  bModernResolutions = true;     // in-game menu lists the modes the monitor supports

  // [Widescreen]
  bool  bHorPlusFov = true;            // keep the 4:3 vertical view and widen it (Hor+)
  bool  bFixHud = true;                // size the HUD by screen height, not width
  bool  bMenu43 = true;                // draw the menu in a centered 4:3 area
  float fFov = 90.0f;                  // horizontal FOV at 4:3; the game uses 90

  // [Mouse]
  bool  bRawInput = true;              // read the mouse with Raw Input instead of the cursor
  float fSensitivity = 1.0f;           // multiplier on mouse movement
  float fVerticalRatio = 1.0f;         // vertical sensitivity relative to horizontal
  bool  bLowLatencyLook = true;        // draw the view with mouse movement the game hasn't applied yet

  // [Weapons]
  bool  bWheelWeapons = true;          // the mouse wheel switches weapons (see weapons.cpp)
  bool  bWheelInvert = false;          // wheel down for the next weapon instead of wheel up
  bool  bWeaponBar = true;             // show the player's weapons in the HUD when switching
  bool  bFixedCrosshair = false;       // crosshair in the middle of the screen (see crosshair.cpp)

  // [Sound]
  int   iMasterVolume = 8;             // the Audio options slider: 0..16, 8 = the original level (see sound.cpp)
  bool  bSoftClipping = true;          // round off the loudest peaks of the mix instead of clipping them

  // [Graphics]
  bool  bVSync = false;                // wait for the monitor's refresh (see graphics.cpp)
  int   iAnisotropy = 16;              // anisotropic filtering level: 1 (off), 2, 4, 8, 16
  int   iAntiAliasing = 0;             // MSAA samples: 0 (off), 2, 4, 8

  // [Fixes]
  bool  bStreamFix = true;             // file streams on 64-bit Windows (see streamfix.cpp)
  bool  bLightFix = true;              // crashes on lights that were never set up (see lightfix.cpp)
  bool  bHideNotPrecached = true;      // no "Not precached" console messages (see texts.cpp)
  bool  bOwnSettingsFile = true;       // the game keeps its own PersistentSymbols file (see settingsfile.cpp)

  // [Gameplay]
  bool  bUnlockCannon = true;          // the cannon can be picked up and selected (see cannon.cpp)
  bool  bTfeThirdPerson = true;        // third-person camera like The First Encounter's (see thirdperson.cpp)
};

extern Config g_cfg;

void LoadConfig();
void SaveConfig();
const char *KeyName(int vk);

// ---------------------------------------------------------------------------
// Logging (AlphaFixes.log next to SeriousSam.exe)
// ---------------------------------------------------------------------------

void LogOpen();
void Log(const char *strFormat, ...);

// ---------------------------------------------------------------------------
// Patching helpers
// ---------------------------------------------------------------------------

std::string GameDir();                                      // folder of SeriousSam.exe, with trailing '\'
bool WriteCode(void *pDest, const void *pSrc, size_t ctBytes);
bool CodeMatches(uintptr_t addr, const void *pExpected, size_t ctBytes);
bool HookFunction(void *pTarget, void *pDetour, void **ppOriginal, const char *strName);
// Replaces an import of hModule (by DLL and function name); returns the old pointer or NULL.
void *HookImport(HMODULE hModule, const char *strDll, const char *strFunction, void *pNew);
void *EngineProc(const char *strMangledName);               // export of Engine.dll

// ---------------------------------------------------------------------------
// Modules
// ---------------------------------------------------------------------------

// display.cpp
struct DisplayResolution {
  int  iWidth, iHeight;
  std::vector<int> aiRefresh;        // refresh rates in Hz, highest first
  std::string strName;               // "1920x1080"
};
bool InstallDisplayFixes();
void ApplyDpiAwareness();
const std::vector<DisplayResolution> &GetResolutions();
void GetDesktopSize(int &iWidth, int &iHeight);
struct VideoMode { int iWidth, iHeight, iDepth, iDriver; bool bFullscreen; };
VideoMode GetCurrentVideoMode();
void RequestVideoMode(const VideoMode &vm);   // applied on the main thread at a safe point
void RunDeferredDisplayWork();                // called from the main loop
HWND GetGameWindow();

// main.cpp
uintptr_t RebaseEntities(uintptr_t ulAddress);   // Entities.dll address (preferred base) -> loaded address
void UpdateLateHooks();                       // hooks Entities.dll once the engine has loaded it

// widescreen.cpp
bool InstallWidescreenFixes();
void InstallWidescreenEntitiesHooks(HMODULE hEntities);
float LastViewAspect();
float WidenPlayerProjection(void *pr);        // CProjection3D: returns the FOV to restore (or NAN)
void RestorePlayerProjection(void *pr, float fOld);

// mouse.cpp
bool InstallMouseFixes();
void SetInputBlocked(bool bBlocked);          // overlay open: the game sees no keys or mouse
bool IsGameInputEnabled();
long PeekRawMouseCount();                     // raw mouse events seen, for the overlay
void InstallMouseEntitiesHooks(HMODULE hEntities);

// overlay.cpp
bool InstallOverlay();
bool IsOverlayVisible();
bool IsOverlayContext(HGLRC hglrc);           // the settings window's own GL context

// streamfix.cpp
void InstallStreamFix();

// lightfix.cpp
void InstallLightFix();
void InstallLightFixEntitiesHooks(HMODULE hEntities);
#if AF_TEST_AIDS
bool BreakLensFlareLight();                   // testing: reproduces the reported crash state
void PoisonUnsetPlayerLight();                // testing: the second report's state, after the next level change
#endif

// ingamemenu.cpp
bool InstallInGameMenu();

// graphics.cpp
void InstallGraphicsOptions();
void UpdateGraphicsOptions();                 // every frame
void ForgetGLContext();                       // the engine deleted its GL context

// crosshair.cpp
void InstallCrosshairEntitiesHooks();
void UpdateCrosshair();                       // every frame: picks up setting changes

// thirdperson.cpp
void InstallThirdPersonEntitiesHooks();

// weapons.cpp
void AddMouseWheel(int iDelta);               // Raw Input thread
void PutWheelInAction(char *pAction);         // after CControls::CreateAction
void InstallWeaponsEntitiesHooks();

// sound.cpp
void InstallSoundFixes();
void UpdateSound();                           // every frame

// settingsfile.cpp
void InstallGameSettingsFile();

// texts.cpp
void InstallTextFixes();

// cannon.cpp
void InstallCannonEntitiesPatches();

// testaids.cpp
#if AF_TEST_AIDS
void InstallTestAids();                       // AF_LOG_CONSOLE
void RunTestAids();                           // main thread, between frames
#endif
