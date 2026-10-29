// Display modes.
//
// - The video menu offers 15 fixed modes, 320x240 to 2048x1536, all 4:3 except
//   1280x1024 and 640x400.  Its list and the two functions that turn a list
//   index into a size and back (SeriousSam.exe) are replaced, so it offers the
//   modes the monitor actually supports, widescreen ones included.
// - The engine asks for fullscreen modes without a refresh rate, so Windows may
//   pick 60 Hz on a faster monitor.  The highest rate of the mode is requested
//   (or the one set in the ini).  16-bit modes, which current drivers don't
//   offer, fall back to 32-bit.
// - Borderless: in fullscreen, keep the desktop mode and let the game's
//   fullscreen window (a topmost popup) cover the screen at desktop size.
// - DPI awareness: without it, Windows scales the window of this 2000 program
//   on a scaled desktop (blurry picture, scaled mouse coordinates).
// - With the Action! recorder injected, a video mode change crashed in the
//   NVIDIA driver unless the game waits after deleting its GL context (below).
#include "alphafixes.h"

#include <algorithm>
#include <deque>

namespace {

// SeriousSam.exe (13 Jan 2000)
constexpr uintptr_t EXE_START_NEW_MODE = 0x423670;   // BOOL StartNewMode(w, h, depth, driver, fullscreen)
constexpr uintptr_t EXE_RES_TO_SIZE = 0x40C4B0;      // void ResolutionToSize(index, &w, &h)
constexpr uintptr_t EXE_SIZE_TO_RES = 0x419320;      // void SizeToResolution(w, h, &index)
constexpr uintptr_t EXE_MENU_FIRSTTEXT = 0x41911A;   // mov eax, [astrResolutionTexts]
constexpr uintptr_t EXE_MENU_TEXTS = 0x41911F;       // mov [mgResolution.mg_astrTexts], astrResolutionTexts
constexpr uintptr_t EXE_MENU_COUNT = 0x41912A;       // mov [mgResolution.mg_ctTexts], 15
constexpr uintptr_t EXE_MENU_COUNT_VAR = 0x4363BC;   // mgResolution.mg_ctTexts
constexpr uintptr_t EXE_MAIN_HWND = 0x432AA4;
float *const sam_bFullScreen = (float *)0x430008;
float *const sam_fScreenSizeI = (float *)0x43000C;
float *const sam_fScreenSizeJ = (float *)0x430010;
float *const sam_fDisplayDepth = (float *)0x441180;
float *const sam_iDriver = (float *)0x441184;

using StartNewMode_t = BOOL(__cdecl *)(int iWidth, int iHeight, int iDepth, int iDriver, int bFullscreen);
using ChangeDisplaySettingsA_t = LONG(WINAPI *)(DEVMODEA *, DWORD);

StartNewMode_t _pfnStartNewMode = nullptr;

// Action! (Mirillis game recorder) injects action_x86.dll, which captures the
// game in its GL context.  A thread of it does a round of work once a second.
// If the game goes on with a video mode change (new display mode, new window,
// new context) before that round has run after the old context was deleted,
// the NVIDIA driver's worker thread later runs code of the deleted context and
// the game crashes.  Waiting 1.5 s right after the delete avoided it in every
// test; 0.1 and 0.4 s did not, nor did waiting only before the new context.
constexpr DWORD RECORDER_SETTLE_MS = 1500;
using EndSubDriver_t = void(__fastcall *)(void *pgl, void *pvEdx, void *pogld);
EndSubDriver_t _pfnEndSubDriver = nullptr;

// CGfxLibrary::EndSubDriver: deletes the GL context and unloads the driver DLL.
void __fastcall EndSubDriver_Hook(void *pgl, void *pvEdx, void *pogld)
{
  _pfnEndSubDriver(pgl, pvEdx, pogld);
  ForgetGLContext();
  if (GetModuleHandleA("action_x86.dll") != nullptr) {
    Log("Action! recorder loaded: waiting %lu ms after deleting the GL context", RECORDER_SETTLE_MS);
    Sleep(RECORDER_SETTLE_MS);
  }
}

ChangeDisplaySettingsA_t _pfnChangeDisplaySettingsA = ChangeDisplaySettingsA;

std::vector<DisplayResolution> _aResolutions;
std::deque<std::string> _astrMenuStorage;            // stable storage for the menu's char pointers
constexpr int MAX_MENU_RESOLUTIONS = 256;
const char *_apstrMenuTexts[MAX_MENU_RESOLUTIONS];
int _ctMenuTexts = 0;

bool _bHavePendingMode = false;
VideoMode _vmPending = {};
bool _bBorderlessActive = false;

void BuildResolutionList()
{
  _aResolutions.clear();
  DEVMODEA dm = {};
  dm.dmSize = sizeof(dm);
  for (DWORD iMode = 0; EnumDisplaySettingsA(nullptr, iMode, &dm); iMode++) {
    if (dm.dmPelsWidth < 640 || dm.dmPelsHeight < 480 || dm.dmBitsPerPel < 16) continue;
    if (dm.dmDisplayFlags & DM_INTERLACED) continue;
    // portrait and odd driver modes (1280x1440...): nothing narrower than 5:4
    if (dm.dmPelsWidth * 4 < dm.dmPelsHeight * 5) continue;
    auto it = std::find_if(_aResolutions.begin(), _aResolutions.end(), [&](const DisplayResolution &r) {
      return r.iWidth == (int)dm.dmPelsWidth && r.iHeight == (int)dm.dmPelsHeight;
    });
    if (it == _aResolutions.end()) {
      _aResolutions.push_back({ (int)dm.dmPelsWidth, (int)dm.dmPelsHeight, {},
                                std::to_string(dm.dmPelsWidth) + "x" + std::to_string(dm.dmPelsHeight) });
      it = _aResolutions.end() - 1;
    }
    int iRefresh = (int)dm.dmDisplayFrequency;
    if (iRefresh > 1 && std::find(it->aiRefresh.begin(), it->aiRefresh.end(), iRefresh) == it->aiRefresh.end()) {
      it->aiRefresh.push_back(iRefresh);
    }
  }
  for (auto &r : _aResolutions) std::sort(r.aiRefresh.rbegin(), r.aiRefresh.rend());
  std::sort(_aResolutions.begin(), _aResolutions.end(), [](const DisplayResolution &a, const DisplayResolution &b) {
    return a.iWidth != b.iWidth ? a.iWidth < b.iWidth : a.iHeight < b.iHeight;
  });
  Log("Display: %d resolutions", (int)_aResolutions.size());
}

int AddMenuResolution(int iWidth, int iHeight)
{
  if (_ctMenuTexts >= MAX_MENU_RESOLUTIONS) return -1;
  _astrMenuStorage.push_back(std::to_string(iWidth) + "x" + std::to_string(iHeight));
  _apstrMenuTexts[_ctMenuTexts] = _astrMenuStorage.back().c_str();
  return _ctMenuTexts++;
}

bool ParseMenuText(const char *str, int &iWidth, int &iHeight)
{
  return sscanf_s(str, "%dx%d", &iWidth, &iHeight) == 2 && iWidth > 0 && iHeight > 0;
}

void __cdecl ResolutionToSize_Hook(int iIndex, int *piWidth, int *piHeight)
{
  int iWidth = 640, iHeight = 480;
  if (iIndex >= 0 && iIndex < _ctMenuTexts) ParseMenuText(_apstrMenuTexts[iIndex], iWidth, iHeight);
  *piWidth = iWidth;
  *piHeight = iHeight;
}

void __cdecl SizeToResolution_Hook(int iWidth, int iHeight, int *piIndex)
{
  for (int i = 0; i < _ctMenuTexts; i++) {
    int iW, iH;
    if (ParseMenuText(_apstrMenuTexts[i], iW, iH) && iW == iWidth && iH == iHeight) {
      *piIndex = i;
      return;
    }
  }
  // a mode that isn't in the list (set in the ini or the overlay): add it
  int iNew = AddMenuResolution(iWidth, iHeight);
  if (iNew >= 0) *(int *)EXE_MENU_COUNT_VAR = _ctMenuTexts;
  *piIndex = iNew >= 0 ? iNew : 0;
}

bool InstallMenuResolutions()
{
  static const BYTE aubFirst[] = { 0xA1, 0x44, 0x63, 0x43, 0x00 };
  static const BYTE aubTexts[] = { 0xC7, 0x05, 0xB8, 0x63, 0x43, 0x00, 0x44, 0x63, 0x43, 0x00 };
  static const BYTE aubCount[] = { 0xC7, 0x05, 0xBC, 0x63, 0x43, 0x00, 0x0F, 0x00, 0x00, 0x00 };
  static const BYTE aubResToSize[] = { 0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x83, 0xF8, 0x0E };
  static const BYTE aubSizeToRes[] = { 0x55, 0x8B, 0xEC, 0x8B, 0x4D, 0x10, 0x8B, 0x45, 0x08 };
  if (!CodeMatches(EXE_MENU_FIRSTTEXT, aubFirst, sizeof(aubFirst)) || !CodeMatches(EXE_MENU_TEXTS, aubTexts, sizeof(aubTexts))
   || !CodeMatches(EXE_MENU_COUNT, aubCount, sizeof(aubCount)) || !CodeMatches(EXE_RES_TO_SIZE, aubResToSize, sizeof(aubResToSize))
   || !CodeMatches(EXE_SIZE_TO_RES, aubSizeToRes, sizeof(aubSizeToRes))) {
    Log("Menu resolutions: code differs, not patched");
    return false;
  }

  for (const auto &r : _aResolutions) AddMenuResolution(r.iWidth, r.iHeight);
  if (_ctMenuTexts == 0) AddMenuResolution(640, 480);

  // point the menu's resolution list at ours
  uint32_t ulTexts = (uint32_t)(uintptr_t)_apstrMenuTexts;
  uint32_t ulCount = (uint32_t)_ctMenuTexts;
  WriteCode((void *)(EXE_MENU_FIRSTTEXT + 1), &ulTexts, 4);
  WriteCode((void *)(EXE_MENU_TEXTS + 6), &ulTexts, 4);
  WriteCode((void *)(EXE_MENU_COUNT + 6), &ulCount, 4);

  void *pvDummy = nullptr;
  bool bOk = HookFunction((void *)EXE_RES_TO_SIZE, (void *)ResolutionToSize_Hook, &pvDummy, "ResolutionToSize");
  bOk &= HookFunction((void *)EXE_SIZE_TO_RES, (void *)SizeToResolution_Hook, &pvDummy, "SizeToResolution");
  Log("Menu resolutions: %d modes", _ctMenuTexts);
  return bOk;
}

BOOL __cdecl StartNewMode_Hook(int iWidth, int iHeight, int iDepth, int iDriver, int bFullscreen)
{
  if (!(bFullscreen && g_cfg.eFullscreenMode == FullscreenMode::Borderless)) {
    Log("Video mode: %dx%d %s, depth %d, driver %d", iWidth, iHeight, bFullscreen ? "fullscreen" : "window", iDepth, iDriver);
    return _pfnStartNewMode(iWidth, iHeight, iDepth, iDriver, bFullscreen);
  }
  // Borderless runs at desktop size, but the game keeps the chosen resolution
  // (sam_fScreenSizeI/J) for the menu and for going back to a window.
  int iDesktopW, iDesktopH;
  GetDesktopSize(iDesktopW, iDesktopH);
  Log("Video mode: %dx%d borderless (chosen %dx%d), depth %d, driver %d", iDesktopW, iDesktopH, iWidth, iHeight, iDepth, iDriver);
  BOOL bResult = _pfnStartNewMode(iDesktopW, iDesktopH, iDepth, iDriver, bFullscreen);
  if (*sam_fScreenSizeI == (float)iDesktopW && *sam_fScreenSizeJ == (float)iDesktopH) {
    *sam_fScreenSizeI = (float)iWidth;
    *sam_fScreenSizeJ = (float)iHeight;
  }
  return bResult;
}

int RefreshRateFor(int iWidth, int iHeight)
{
  if (g_cfg.iRefreshRate > 0) return g_cfg.iRefreshRate;
  for (const auto &r : _aResolutions) {
    if (r.iWidth == iWidth && r.iHeight == iHeight) return r.aiRefresh.empty() ? 0 : r.aiRefresh.front();
  }
  return 0;
}

// Engine.dll's ChangeDisplaySettingsA: (w, h[, bpp]) with CDS_FULLSCREEN, or NULL to restore.
LONG WINAPI ChangeDisplaySettingsA_Hook(DEVMODEA *pdm, DWORD dwFlags)
{
  if (pdm == nullptr) {
    if (_bBorderlessActive) {
      _bBorderlessActive = false;
      return DISP_CHANGE_SUCCESSFUL;
    }
    return _pfnChangeDisplaySettingsA(pdm, dwFlags);
  }

  int iDesktopW, iDesktopH;
  GetDesktopSize(iDesktopW, iDesktopH);
  if (g_cfg.eFullscreenMode == FullscreenMode::Borderless && (dwFlags & CDS_FULLSCREEN)
   && (int)pdm->dmPelsWidth == iDesktopW && (int)pdm->dmPelsHeight == iDesktopH) {
    _bBorderlessActive = true;
    Log("Display: borderless %dx%d", iDesktopW, iDesktopH);
    return DISP_CHANGE_SUCCESSFUL;
  }
  _bBorderlessActive = false;

  DEVMODEA dm = {};
  memcpy(&dm, pdm, std::min<size_t>(pdm->dmSize, sizeof(dm)));
  dm.dmSize = sizeof(dm);
  if ((dm.dmFields & DM_BITSPERPEL) && dm.dmBitsPerPel < 32) {
    dm.dmBitsPerPel = 32;          // no 15/16-bit desktop modes on current drivers
  }
  int iRefresh = RefreshRateFor((int)dm.dmPelsWidth, (int)dm.dmPelsHeight);
  if (iRefresh > 0) {
    dm.dmDisplayFrequency = iRefresh;
    dm.dmFields |= DM_DISPLAYFREQUENCY;
    LONG lResult = _pfnChangeDisplaySettingsA(&dm, dwFlags);
    if (!(dwFlags & CDS_TEST)) Log("Display: %ux%u @ %d Hz -> %ld", dm.dmPelsWidth, dm.dmPelsHeight, iRefresh, lResult);
    if (lResult == DISP_CHANGE_SUCCESSFUL) return lResult;
    dm.dmFields &= ~DM_DISPLAYFREQUENCY;
  }
  LONG lResult = _pfnChangeDisplaySettingsA(&dm, dwFlags);
  if (lResult != DISP_CHANGE_SUCCESSFUL) lResult = _pfnChangeDisplaySettingsA(pdm, dwFlags);
  if (!(dwFlags & CDS_TEST)) Log("Display: %ux%u -> %ld", dm.dmPelsWidth, dm.dmPelsHeight, lResult);
  return lResult;
}

}  // namespace

void ApplyDpiAwareness()
{
  if (!g_cfg.bDpiAware) return;
  using SetContext_t = BOOL(WINAPI *)(HANDLE);
  auto pfnSetContext = (SetContext_t)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
  if (pfnSetContext != nullptr && pfnSetContext((HANDLE)-4)) {   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    Log("DPI awareness: per monitor");
    return;
  }
  Log("DPI awareness: system (%d)", SetProcessDPIAware());
}

bool InstallDisplayFixes()
{
  BuildResolutionList();

  static const BYTE aubStartNewMode[] = { 0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x75, 0x14 };
  bool bOk = CodeMatches(EXE_START_NEW_MODE, aubStartNewMode, sizeof(aubStartNewMode))
          && HookFunction((void *)EXE_START_NEW_MODE, (void *)StartNewMode_Hook, (void **)&_pfnStartNewMode, "StartNewMode");
  if (g_cfg.bModernResolutions) bOk &= InstallMenuResolutions();
  bOk &= HookFunction(EngineProc("?EndSubDriver@CGfxLibrary@@QAEXAAVCOpenGLDriver@@@Z"), (void *)EndSubDriver_Hook,
                      (void **)&_pfnEndSubDriver, "EndSubDriver");

  if (void *p = HookImport(GetModuleHandleA("Engine.dll"), "USER32.dll", "ChangeDisplaySettingsA",
                           (void *)ChangeDisplaySettingsA_Hook)) {
    _pfnChangeDisplaySettingsA = (ChangeDisplaySettingsA_t)p;
  } else {
    bOk = false;
  }
  return bOk;
}

const std::vector<DisplayResolution> &GetResolutions() { return _aResolutions; }

void GetDesktopSize(int &iWidth, int &iHeight)
{
  DEVMODEA dm = {};
  dm.dmSize = sizeof(dm);
  if (EnumDisplaySettingsA(nullptr, ENUM_REGISTRY_SETTINGS, &dm)) {
    iWidth = (int)dm.dmPelsWidth;
    iHeight = (int)dm.dmPelsHeight;
  } else {
    iWidth = GetSystemMetrics(SM_CXSCREEN);
    iHeight = GetSystemMetrics(SM_CYSCREEN);
  }
}

VideoMode GetCurrentVideoMode()
{
  VideoMode vm;
  vm.iWidth = (int)*sam_fScreenSizeI;
  vm.iHeight = (int)*sam_fScreenSizeJ;
  vm.iDepth = (int)*sam_fDisplayDepth;
  vm.iDriver = (int)*sam_iDriver;
  vm.bFullscreen = *sam_bFullScreen != 0.0f;
  return vm;
}

void RequestVideoMode(const VideoMode &vm)
{
  _vmPending = vm;
  _bHavePendingMode = true;
}

// From the main loop, between frames (the same place the game handles Alt+Enter).
void RunDeferredDisplayWork()
{
  if (!_bHavePendingMode || _pfnStartNewMode == nullptr) return;
  _bHavePendingMode = false;
  ((StartNewMode_t)EXE_START_NEW_MODE)(_vmPending.iWidth, _vmPending.iHeight, _vmPending.iDepth,
                                       _vmPending.iDriver, _vmPending.bFullscreen);
}

HWND GetGameWindow() { return *(HWND *)EXE_MAIN_HWND; }
