// Settings overlay (Dear ImGui).
//
// Drawn on top of each frame in a hook of GDI32!SwapBuffers, which the engine
// calls to present a frame.  The overlay renders with an OpenGL context of its
// own on the game's window, so it never touches the game's GL state or
// textures; the game's context is made current again before the swap.  Display
// mode changes destroy the game's window, and the overlay's context and window
// bindings follow the new one.
//
// Keyboard and mouse messages are not dispatched by the game: its main loop
// takes them straight from PeekMessageA (SeriousSam.exe's import).  That import
// is hooked to open/close the overlay with the hotkey and, while it is open,
// to pass input to ImGui and hide it from the game.  The same hook runs queued
// display mode changes on the main thread, between frames.
#include "alphafixes.h"

#include "imgui.h"
#include "backends/imgui_impl_opengl2.h"
#include "backends/imgui_impl_win32.h"

#include <GL/gl.h>
#include <cmath>
#include <cstdio>
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

// the main loop's two PeekMessageA calls; when either finds the queue empty, the frame starts
constexpr uintptr_t EXE_MAINLOOP_PEEK_RETURN1 = 0x423007;
constexpr uintptr_t EXE_MAINLOOP_PEEK_RETURN2 = 0x423477;

using SwapBuffers_t = BOOL(WINAPI *)(HDC);
using PeekMessageA_t = BOOL(WINAPI *)(LPMSG, HWND, UINT, UINT, UINT);
SwapBuffers_t _pfnSwapBuffers = SwapBuffers;
PeekMessageA_t _pfnPeekMessageA = PeekMessageA;

bool _bVisible = false;
bool _bImGuiReady = false;
HDC _hdcOverlay = nullptr;
HGLRC _hglrcOverlay = nullptr;
bool _bRendererReady = false;
HWND _hwndPlatform = nullptr;
float _fUiScale = 0.0f;

LARGE_INTEGER _llFreq = {}, _llLastSwap = {};
float _fFrameMs = 0.0f;

// overlay copy of the video mode being edited
int _iSelResolution = -1;
int _iSelRefresh = 0;               // 0 = highest
bool _bSelFullscreen = true;
bool _bEditInit = false;
std::string _strStatus;

// --- frame timing -----------------------------------------------------------

void UpdateFrameTime()
{
  LARGE_INTEGER llNow;
  QueryPerformanceCounter(&llNow);
  if (_llFreq.QuadPart == 0) QueryPerformanceFrequency(&_llFreq);
  if (_llLastSwap.QuadPart != 0) {
    float fMs = (float)((llNow.QuadPart - _llLastSwap.QuadPart) * 1000.0 / _llFreq.QuadPart);
    _fFrameMs = _fFrameMs == 0.0f ? fMs : _fFrameMs * 0.95f + fMs * 0.05f;
  }
  _llLastSwap = llNow;
}

// --- UI -----------------------------------------------------------------------

std::string AspectName(int iWidth, int iHeight)
{
  struct { int w, h; const char *str; } aKnown[] = {
    { 4, 3, "4:3" }, { 5, 4, "5:4" }, { 16, 9, "16:9" }, { 16, 10, "16:10" }, { 21, 9, "21:9" },
    { 32, 9, "32:9" }, { 3, 2, "3:2" }, { 5, 3, "5:3" },
  };
  float fAspect = (float)iWidth / (float)iHeight;
  for (const auto &k : aKnown) {
    if (fabsf(fAspect - (float)k.w / k.h) < 0.02f) return k.str;
  }
  if (fabsf(fAspect - 64.0f / 27.0f) < 0.05f) return "21:9";
  char str[16];
  snprintf(str, sizeof(str), "%.2f:1", fAspect);
  return str;
}

std::string ResolutionLabel(int iWidth, int iHeight)
{
  return std::to_string(iWidth) + " x " + std::to_string(iHeight) + "  (" + AspectName(iWidth, iHeight) + ")";
}

void InitEditFromGame()
{
  VideoMode vm = GetCurrentVideoMode();
  const auto &aRes = GetResolutions();
  _iSelResolution = -1;
  for (int i = 0; i < (int)aRes.size(); i++) {
    if (aRes[i].iWidth == vm.iWidth && aRes[i].iHeight == vm.iHeight) _iSelResolution = i;
  }
  _bSelFullscreen = vm.bFullscreen;
  _iSelRefresh = g_cfg.iRefreshRate;
  _bEditInit = true;
}

bool SliderLog(const char *strLabel, float *pf, float fMin, float fMax, const char *strFormat)
{
  return ImGui::SliderFloat(strLabel, pf, fMin, fMax, strFormat, ImGuiSliderFlags_Logarithmic);
}

void HelpMarker(const char *str)
{
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::BeginItemTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(str);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void DisplaySection()
{
  VideoMode vm = GetCurrentVideoMode();
  const auto &aRes = GetResolutions();
  ImGui::Text("Current: %s, %s", ResolutionLabel(vm.iWidth, vm.iHeight).c_str(), vm.bFullscreen ? "fullscreen" : "window");

  std::string strPreview = _iSelResolution >= 0 ? ResolutionLabel(aRes[_iSelResolution].iWidth, aRes[_iSelResolution].iHeight)
                                                : ResolutionLabel(vm.iWidth, vm.iHeight);
  if (ImGui::BeginCombo("Resolution", strPreview.c_str(), ImGuiComboFlags_HeightLarge)) {
    for (int i = (int)aRes.size() - 1; i >= 0; i--) {
      bool bSelected = i == _iSelResolution;
      if (ImGui::Selectable(ResolutionLabel(aRes[i].iWidth, aRes[i].iHeight).c_str(), bSelected)) _iSelResolution = i;
      if (bSelected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  ImGui::Checkbox("Fullscreen", &_bSelFullscreen);
  int iMode = (int)g_cfg.eFullscreenMode;
  ImGui::SameLine();
  ImGui::BeginDisabled(!_bSelFullscreen);
  ImGui::RadioButton("Exclusive", &iMode, (int)FullscreenMode::Exclusive);
  ImGui::SameLine();
  ImGui::RadioButton("Borderless", &iMode, (int)FullscreenMode::Borderless);
  HelpMarker("Exclusive changes the monitor's display mode, like the original game.\n"
             "Borderless keeps the desktop mode and covers the screen with the game "
             "at desktop resolution; the resolution above is then only used in a window.");
  g_cfg.eFullscreenMode = (FullscreenMode)iMode;

  bool bExclusive = g_cfg.eFullscreenMode == FullscreenMode::Exclusive;
  ImGui::BeginDisabled(!bExclusive);
  const std::vector<int> *paiRates = _iSelResolution >= 0 ? &aRes[_iSelResolution].aiRefresh : nullptr;
  char strRate[32];
  if (_iSelRefresh <= 0) {
    snprintf(strRate, sizeof(strRate), "Highest%s", (paiRates && !paiRates->empty()) ? "" : " available");
    if (paiRates && !paiRates->empty()) snprintf(strRate, sizeof(strRate), "Highest (%d Hz)", paiRates->front());
  } else {
    snprintf(strRate, sizeof(strRate), "%d Hz", _iSelRefresh);
  }
  if (ImGui::BeginCombo("Refresh rate", strRate)) {
    if (ImGui::Selectable("Highest", _iSelRefresh <= 0)) _iSelRefresh = 0;
    if (paiRates) {
      for (int iRate : *paiRates) {
        char str[16];
        snprintf(str, sizeof(str), "%d Hz", iRate);
        if (ImGui::Selectable(str, iRate == _iSelRefresh)) _iSelRefresh = iRate;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
  ImGui::EndDisabled();

  if (ImGui::Button("Apply video mode")) {
    VideoMode vmNew = vm;
    if (_iSelResolution >= 0) {
      vmNew.iWidth = aRes[_iSelResolution].iWidth;
      vmNew.iHeight = aRes[_iSelResolution].iHeight;
    }
    vmNew.bFullscreen = _bSelFullscreen;
    g_cfg.iRefreshRate = _iSelRefresh;
    SaveConfig();
    RequestVideoMode(vmNew);
    _strStatus = "Video mode applied.";
  }
  ImGui::SameLine();
  ImGui::TextDisabled("The game's own Options > Video menu also lists these modes.");

  ImGui::Spacing();
  ImGui::Checkbox("Show these resolutions in the game's video menu", &g_cfg.bModernResolutions);
  HelpMarker("Takes effect the next time the game starts.");
  ImGui::Checkbox("DPI aware", &g_cfg.bDpiAware);
  HelpMarker("Stops Windows from scaling the game window on a scaled desktop, which blurs the "
             "picture and the mouse.  Takes effect the next time the game starts.");
}

void GraphicsSection()
{
  ImGui::Checkbox("Vertical sync", &g_cfg.bVSync);
  HelpMarker("Waits for the monitor's refresh before showing a frame: no tearing, frame rate up to the refresh rate.");

  static const int aiAniso[] = { 1, 2, 4, 8, 16 };
  static const char *const astrAniso[] = { "Off", "2x", "4x", "8x", "16x" };
  int iAniso = 0;
  while (iAniso < 4 && aiAniso[iAniso] < g_cfg.iAnisotropy) iAniso++;
  if (ImGui::Combo("Anisotropic filtering", &iAniso, astrAniso, 5)) g_cfg.iAnisotropy = aiAniso[iAniso];
  HelpMarker("Keeps floors and walls sharp at a glancing angle.  Applies at once.");

  static const int aiAA[] = { 0, 2, 4, 8 };
  static const char *const astrAA[] = { "Off", "2x MSAA", "4x MSAA", "8x MSAA" };
  int iAA = 0;
  while (iAA < 3 && aiAA[iAA] < g_cfg.iAntiAliasing) iAA++;
  if (ImGui::Combo("Anti-aliasing", &iAA, astrAA, 4)) {
    g_cfg.iAntiAliasing = aiAA[iAA];
    SaveConfig();
    RequestVideoMode(GetCurrentVideoMode());   // a new pixel format needs a new window
    _strStatus = "Anti-aliasing applied: video mode reset.";
  }
  HelpMarker("Smooths the edges of polygons.  Changing it resets the video mode.");
}

void SoundSection()
{
  ImGui::SliderInt("Master volume", &g_cfg.iMasterVolume, 0, 16, "%d / 16");
  HelpMarker("The same as Options > Audio options > Master volume.  8 is the original level.");
  ImGui::SameLine();
  if (ImGui::SmallButton("8")) g_cfg.iMasterVolume = 8;
  ImGui::Checkbox("Soft clipping", &g_cfg.bSoftClipping);
  HelpMarker("Rounds off the loudest peaks instead of clipping them hard, so many loud sounds "
             "at once don't distort.  Off: a plain clamp, like the original.");
}

void WidescreenSection()
{
  ImGui::Checkbox("Hor+ field of view", &g_cfg.bHorPlusFov);
  HelpMarker("On screens wider than 4:3 the game crops the top and bottom of the view.  "
             "Hor+ keeps the 4:3 view height and shows more at the sides instead.");
  ImGui::SliderFloat("Field of view", &g_cfg.fFov, 60.0f, 130.0f, "%.0f deg");
  HelpMarker("Horizontal FOV of a 4:3 screen.  The game uses 90; Hor+ widens it for wider screens.  "
             "Weapon zoom keeps its own FOV.");
  ImGui::SameLine();
  if (ImGui::SmallButton("90")) g_cfg.fFov = 90.0f;
  // full angle <-> tangent of the half angle
  auto TanHalf = [](float fDeg) { return tanf(fDeg * 3.14159265f / 360.0f); };
  auto FullAngle = [](float fTanHalf) { return atanf(fTanHalf) * 360.0f / 3.14159265f; };
  float fAspect = LastViewAspect();
  float fHor = g_cfg.fFov;
  if (g_cfg.bHorPlusFov && fAspect > 4.0f / 3.0f) fHor = FullAngle(TanHalf(fHor) * fAspect * 0.75f);
  float fVer = FullAngle(TanHalf(fHor) / fAspect);
  ImGui::TextDisabled("View %.2f:1 -> %.1f deg horizontal, %.1f deg vertical", fAspect, fHor, fVer);
  ImGui::Checkbox("Size the HUD by screen height", &g_cfg.bFixHud);
  HelpMarker("The game sizes the HUD and the crosshair by screen width, which makes them too big on wide screens.");
  ImGui::Checkbox("Keep the menu 4:3", &g_cfg.bMenu43);
  HelpMarker("Draws the menu in a 4:3 area in the middle of the screen instead of stretching it.");
}

void MouseSection()
{
  ImGui::Checkbox("Raw mouse input", &g_cfg.bRawInput);
  HelpMarker("Reads the mouse directly (Raw Input) instead of through the Windows cursor: "
             "no pointer speed or acceleration, no lost movement on fast turns, "
             "no desktop scaling.  Off: the game's original cursor-based input.");
  SliderLog("Sensitivity", &g_cfg.fSensitivity, 0.05f, 10.0f, "%.3f");
  ImGui::SameLine();
  if (ImGui::SmallButton("1.0")) g_cfg.fSensitivity = 1.0f;
  SliderLog("Vertical ratio", &g_cfg.fVerticalRatio, 0.25f, 4.0f, "%.2f");
  HelpMarker("Vertical sensitivity relative to horizontal.");
  ImGui::SameLine();
  if (ImGui::SmallButton("1.00")) g_cfg.fVerticalRatio = 1.0f;
  ImGui::TextDisabled("1.0 = one mouse count per cursor pixel of the original game.  The per-axis\n"
                      "sensitivity and invert settings in Options > Players and controls still apply.");
  ImGui::Checkbox("Low-latency mouse look", &g_cfg.bLowLatencyLook);
  HelpMarker("The game turns the player 20 times a second and shows the view 50-100 ms behind the mouse.  "
             "This draws the view where the game is about to turn it, so it follows the mouse at once.  "
             "Aiming and shooting are unchanged.");
  ImGui::Checkbox("Mouse wheel switches weapons", &g_cfg.bWheelWeapons);
  HelpMarker("Wheel up: next weapon, wheel down: previous (Invert swaps them), among the weapons you have ammo for.");
  ImGui::SameLine();
  ImGui::Checkbox("Invert", &g_cfg.bWheelInvert);
  ImGui::Checkbox("Show the weapons in the HUD when switching", &g_cfg.bWeaponBar);
  ImGui::Checkbox("Fixed crosshair", &g_cfg.bFixedCrosshair);
  HelpMarker("Keeps the crosshair in the middle of the screen.  The game moves it to where the weapon's "
             "ray hits, so it jumps around near walls and edges.  Size and colour still show what you aim at.");
  ImGui::Checkbox("First Encounter third-person camera", &g_cfg.bTfeThirdPerson);
  HelpMarker("Third person (the view key): the camera looks at what you aim at, so the crosshair stays in the "
             "middle, and keeps out of walls and enemies.  Off: the alpha's camera.");
  ImGui::TextDisabled("Raw mouse events received: %ld", PeekRawMouseCount());
}

void OtherSection()
{
  ImGui::Checkbox("Show frame rate", &g_cfg.bShowFps);
  ImGui::Checkbox("Pause the game in the menu", &g_cfg.bMenuPause);
  HelpMarker("Esc pauses single-player and split-screen games while the menu is up.  "
             "Network games go on, as in the alpha.");
  ImGui::Checkbox("AlphaFixes page in the game's Options menu", &g_cfg.bInGameMenu);
  HelpMarker("Takes effect the next time the game starts.");
  ImGui::Text("Settings window key: %s (ToggleKey in AlphaFixes.ini)", KeyName(g_cfg.iToggleKey));
  ImGui::TextDisabled("Frame time %.2f ms (%.0f fps)", _fFrameMs, _fFrameMs > 0 ? 1000.0f / _fFrameMs : 0.0f);
}

void BuildUi()
{
  ImGuiIO &io = ImGui::GetIO();
  if (_bVisible) {
    if (!_bEditInit) InitEditFromGame();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 34.0f, 0), ImVec2(FLT_MAX, io.DisplaySize.y * 0.9f));
    if (ImGui::Begin("AlphaFixes", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
      ImGui::TextDisabled("%s or Esc closes this window. Settings are saved to AlphaFixes.ini.", KeyName(g_cfg.iToggleKey));
      ImGui::SetNextItemOpen(true, ImGuiCond_Once);
      if (ImGui::CollapsingHeader("Display")) DisplaySection();
      ImGui::SetNextItemOpen(true, ImGuiCond_Once);
      if (ImGui::CollapsingHeader("Graphics")) GraphicsSection();
      if (ImGui::CollapsingHeader("Sound")) SoundSection();
      ImGui::SetNextItemOpen(true, ImGuiCond_Once);
      if (ImGui::CollapsingHeader("Widescreen")) WidescreenSection();
      ImGui::SetNextItemOpen(true, ImGuiCond_Once);
      if (ImGui::CollapsingHeader("Mouse")) MouseSection();
      if (ImGui::CollapsingHeader("Other")) OtherSection();
      if (!_strStatus.empty()) ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", _strStatus.c_str());
    }
    ImGui::End();
  }
  if (g_cfg.bShowFps && _fFrameMs > 0.0f) {
    char str[32];
    snprintf(str, sizeof(str), "%.0f fps", 1000.0f / _fFrameMs);
    ImDrawList *pdl = ImGui::GetForegroundDrawList();
    ImVec2 vSize = ImGui::CalcTextSize(str);
    ImVec2 vPos(io.DisplaySize.x - vSize.x - 8.0f, 6.0f);
    pdl->AddText(ImVec2(vPos.x + 1, vPos.y + 1), IM_COL32(0, 0, 0, 200), str);
    pdl->AddText(vPos, IM_COL32(255, 255, 160, 255), str);
  }
}

// --- rendering ----------------------------------------------------------------

void ApplyUiScale(float fScale)
{
  ImGuiIO &io = ImGui::GetIO();
  io.Fonts->Clear();
  char strFont[MAX_PATH];
  GetWindowsDirectoryA(strFont, MAX_PATH);
  strcat_s(strFont, "\\Fonts\\segoeui.ttf");
  ImFont *pFont = nullptr;
  if (GetFileAttributesA(strFont) != INVALID_FILE_ATTRIBUTES) pFont = io.Fonts->AddFontFromFileTTF(strFont, 17.0f * fScale);
  if (pFont == nullptr) {
    ImFontConfig fc;
    fc.SizePixels = 13.0f * fScale;
    io.Fonts->AddFontDefault(&fc);
  }
  ImGuiStyle &style = ImGui::GetStyle();
  style = ImGuiStyle();
  ImGui::StyleColorsDark(&style);
  style.WindowRounding = 6.0f;
  style.FrameRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.Colors[ImGuiCol_WindowBg].w = 0.94f;
  style.ScaleAllSizes(fScale);
  if (_bRendererReady) {
    ImGui_ImplOpenGL2_DestroyFontsTexture();
    ImGui_ImplOpenGL2_CreateFontsTexture();
  }
  _fUiScale = fScale;
}

#if AF_TEST_AIDS
void SaveScreenshot(int iWidth, int iHeight)
{
  std::string strDir = GameDir() + "..\\ScreenShots\\";
  CreateDirectoryA(strDir.c_str(), nullptr);
  char strName[64];
  SYSTEMTIME st;
  GetLocalTime(&st);
  snprintf(strName, sizeof(strName), "AlphaFixes_%04d%02d%02d_%02d%02d%02d_%03d.bmp", st.wYear, st.wMonth, st.wDay,
           st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
  int iStride = (iWidth * 3 + 3) & ~3;
  std::vector<BYTE> aub((size_t)iStride * iHeight);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, iWidth, iHeight, 0x80E0 /* GL_BGR_EXT */, GL_UNSIGNED_BYTE, aub.data());

  BITMAPFILEHEADER bfh = {};
  BITMAPINFOHEADER bih = {};
  bih.biSize = sizeof(bih);
  bih.biWidth = iWidth;
  bih.biHeight = iHeight;
  bih.biPlanes = 1;
  bih.biBitCount = 24;
  bfh.bfType = 0x4D42;
  bfh.bfOffBits = sizeof(bfh) + sizeof(bih);
  bfh.bfSize = bfh.bfOffBits + (DWORD)aub.size();
  FILE *pf = fopen((strDir + strName).c_str(), "wb");
  if (pf == nullptr) return;
  fwrite(&bfh, sizeof(bfh), 1, pf);
  fwrite(&bih, sizeof(bih), 1, pf);
  fwrite(aub.data(), aub.size(), 1, pf);
  fclose(pf);
  Log("Screenshot %s", strName);
}

// A file named AlphaFixes.shot in Bin asks for a screenshot of the next frame (testing aid).
bool ScreenshotRequested()
{
  static DWORD _dwLastCheck = 0;
  DWORD dwNow = GetTickCount();
  if (dwNow - _dwLastCheck < 250) return false;
  _dwLastCheck = dwNow;
  std::string strFile = GameDir() + "AlphaFixes.shot";
  if (GetFileAttributesA(strFile.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
  DeleteFileA(strFile.c_str());
  return true;
}
#else
bool ScreenshotRequested() { return false; }
#endif

// Mouse buttons are read directly: the game's message loop doesn't get every
// button message (the engine's child window covers the client area), and the
// game discards them anyway.  The position is polled by the Win32 backend.
void PollMouse(HWND hwndMain)
{
  static bool _abDown[3] = {};
  bool bSwap = GetSystemMetrics(SM_SWAPBUTTON) != 0;
  const int aiVk[3] = { bSwap ? VK_RBUTTON : VK_LBUTTON, bSwap ? VK_LBUTTON : VK_RBUTTON, VK_MBUTTON };
  bool bActive = _bVisible && GetForegroundWindow() == hwndMain;
  for (int i = 0; i < 3; i++) {
    bool bDown = bActive && (GetAsyncKeyState(aiVk[i]) & 0x8000) != 0;
    if (bDown != _abDown[i]) {
      ImGui::GetIO().AddMouseButtonEvent(i, bDown);
      _abDown[i] = bDown;
    }
  }
}

void DrawOverlay(HDC hdc, bool bScreenshot)
{
  HWND hwndView = WindowFromDC(hdc);
  if (hwndView == nullptr) return;
  HWND hwndMain = GetAncestor(hwndView, GA_ROOT);
  RECT rc;
  GetClientRect(hwndView, &rc);
  int iWidth = rc.right - rc.left, iHeight = rc.bottom - rc.top;
  if (iWidth <= 0 || iHeight <= 0) return;

  HGLRC hglrcGame = wglGetCurrentContext();
  HDC hdcGame = wglGetCurrentDC();

  // a new window (display mode change) gets a new overlay context
  if (hdc != _hdcOverlay || _hglrcOverlay == nullptr) {
    if (_hglrcOverlay != nullptr) {
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(_hglrcOverlay);
    }
    _hglrcOverlay = wglCreateContext(hdc);
    _hdcOverlay = hdc;
    _bRendererReady = false;
    if (_hglrcOverlay == nullptr) {
      Log("Overlay: wglCreateContext failed (%lu)", GetLastError());
      wglMakeCurrent(hdcGame, hglrcGame);
      return;
    }
  }
  if (!wglMakeCurrent(hdc, _hglrcOverlay)) {
    wglMakeCurrent(hdcGame, hglrcGame);
    return;
  }

  if (!_bRendererReady) {
    if (ImGui::GetIO().BackendRendererUserData != nullptr) ImGui_ImplOpenGL2_Shutdown();   // its texture died with the old context
    ImGui_ImplOpenGL2_Init();
    _bRendererReady = true;
  }
  if (hwndMain != _hwndPlatform) {
    if (_hwndPlatform != nullptr) ImGui_ImplWin32_Shutdown();
    ImGui_ImplWin32_Init(hwndMain);
    _hwndPlatform = hwndMain;
  }
  float fScale = floorf(iHeight / 1080.0f * 4.0f + 0.5f) / 4.0f;   // quarter steps
  if (fScale < 1.0f) fScale = 1.0f;
  if (fScale != _fUiScale) ApplyUiScale(fScale);

  // the game hides the Windows cursor while it has the mouse
  CURSORINFO ci = { sizeof(ci) };
  ImGui::GetIO().MouseDrawCursor = _bVisible && GetCursorInfo(&ci) && !(ci.flags & CURSOR_SHOWING);

  PollMouse(hwndMain);
  ImGui_ImplOpenGL2_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
  BuildUi();
  ImGui::Render();
  glViewport(0, 0, iWidth, iHeight);
  ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
#if AF_TEST_AIDS
  if (bScreenshot) SaveScreenshot(iWidth, iHeight);
#endif

  wglMakeCurrent(hdcGame, hglrcGame);
}

BOOL WINAPI SwapBuffers_Hook(HDC hdc)
{
  UpdateFrameTime();
  UpdateLateHooks();
  UpdateGraphicsOptions();
  UpdateSound();
  bool bScreenshot = ScreenshotRequested();
  if (_bImGuiReady && (_bVisible || g_cfg.bShowFps || bScreenshot)) DrawOverlay(hdc, bScreenshot);
  return _pfnSwapBuffers(hdc);
}

// --- input ----------------------------------------------------------------------

void SetVisible(bool bVisible)
{
  if (bVisible == _bVisible) return;
  _bVisible = bVisible;
  SetInputBlocked(bVisible);
  if (bVisible) {
    _bEditInit = false;
    _strStatus.clear();
  } else {
    SaveConfig();
  }
}

bool IsInputMessage(UINT uMsg)
{
  return (uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST) || (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST);
}

// Returns true when the message is for the overlay and the game must not see it.
bool HandleMessage(MSG *pmsg)
{
  UINT uMsg = pmsg->message;
  bool bKeyDown = uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN;
  bool bKeyUp = uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP;
  bool bFirstPress = bKeyDown && !(pmsg->lParam & (1 << 30));

  if ((bKeyDown || bKeyUp) && (int)pmsg->wParam == g_cfg.iToggleKey) {
    if (bFirstPress) SetVisible(!_bVisible);
    return true;
  }
  if (!_bVisible) return false;

  if (bFirstPress && pmsg->wParam == VK_ESCAPE && !ImGui::GetIO().WantTextInput) {
    SetVisible(false);
    return true;
  }
  if (!IsInputMessage(uMsg) && uMsg != WM_CHAR) return false;
  // Mouse position and buttons are polled in PollMouse(); only the wheel comes from messages.
  bool bMouse = uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST;
  bool bWheel = uMsg == WM_MOUSEWHEEL || uMsg == WM_MOUSEHWHEEL;
  if (_hwndPlatform != nullptr && (!bMouse || bWheel)) {
    ImGui_ImplWin32_WndProcHandler(_hwndPlatform, uMsg, pmsg->wParam, pmsg->lParam);
    // the game doesn't translate key messages, so make the WM_CHARs for text fields here
    if (uMsg == WM_KEYDOWN) TranslateMessage(pmsg);
  }
  return true;
}

BOOL WINAPI PeekMessageA_Hook(LPMSG pmsg, HWND hwnd, UINT uMin, UINT uMax, UINT uRemove)
{
  BOOL bResult = _pfnPeekMessageA(pmsg, hwnd, uMin, uMax, uRemove);
  if (!bResult) {
    // queue empty: the main loop goes on to the game and rendering
    void *pvReturn = _ReturnAddress();
    if (pvReturn == (void *)EXE_MAINLOOP_PEEK_RETURN1 || pvReturn == (void *)EXE_MAINLOOP_PEEK_RETURN2) {
      RunDeferredDisplayWork();
#if AF_TEST_AIDS
      RunTestAids();
#endif
    }
    return bResult;
  }
  if ((uRemove & PM_REMOVE) && _bImGuiReady && HandleMessage(pmsg)) {
    pmsg->message = WM_NULL;
  }
  return bResult;
}

}  // namespace

bool IsOverlayVisible() { return _bVisible; }
bool IsOverlayContext(HGLRC hglrc) { return hglrc != nullptr && hglrc == _hglrcOverlay; }

bool InstallOverlay()
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  _bImGuiReady = true;

  bool bOk = true;
  if (void *p = HookImport(GetModuleHandleA("Engine.dll"), "GDI32.dll", "SwapBuffers", (void *)SwapBuffers_Hook)) {
    _pfnSwapBuffers = (SwapBuffers_t)p;
  } else {
    bOk = false;
  }
  if (void *p = HookImport(GetModuleHandleA(nullptr), "USER32.dll", "PeekMessageA", (void *)PeekMessageA_Hook)) {
    _pfnPeekMessageA = (PeekMessageA_t)p;
  } else {
    bOk = false;
  }
  return bOk;
}
