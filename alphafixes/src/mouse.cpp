// Mouse input.
//
// The alpha reads the mouse in CInput::GetInput() (Engine.dll), which the game
// calls from its 20 Hz timer thread: it takes GetCursorPos() minus the window
// center as the movement, then puts the cursor back in the center with
// SetCursorPos().  The cursor is clipped to the window, so everything moved
// within one 50 ms tick has to fit in half the window: at 1024x768 that is
// 384 pixels up or down, about 6 mm of a 1600 dpi mouse.  Faster movements are
// cut off, which feels like strong negative acceleration.  The cursor path also
// goes through the Windows pointer speed and "Enhance pointer precision"
// settings, and on a scaled (high DPI) desktop through Windows' coordinate
// scaling, which loses small movements.
//
// Here the mouse is read with Raw Input on a thread of its own: every count
// the mouse reports is added up, and after the engine's GetInput() the sums
// replace its mouse X/Y axis values.  Sensitivity scales them.  The engine's
// own cursor handling still runs, so the cursor stays hidden in the window.
//
// Low-latency mouse look: see the section at the end of this file.
#include "alphafixes.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace {

// CInput layout (alpha build)
constexpr size_t INP_ENABLED = 0x0;            // inp_bInputEnabled
constexpr size_t INP_MOUSEX_REL = 0x100;       // axis 1 (mouse X): relative, then absolute
constexpr size_t INP_MOUSEX_ABS = 0x104;
constexpr size_t INP_MOUSEY_REL = 0x134;       // axis 2 (mouse Y)
constexpr size_t INP_MOUSEY_ABS = 0x138;
constexpr size_t INP_CENTER_X = 0x140C;        // window center the cursor is put back to
constexpr size_t INP_CENTER_Y = 0x1410;

using GetInput_t = void(__fastcall *)(char *pInput, void *pvEdx);
GetInput_t _pfnGetInput = nullptr;
char **_ppInput = nullptr;                     // &_pInput

using GetAsyncKeyState_t = SHORT(WINAPI *)(int);
using GetCursorPos_t = BOOL(WINAPI *)(LPPOINT);
using SetCursorPos_t = BOOL(WINAPI *)(int, int);
using ClipCursor_t = BOOL(WINAPI *)(const RECT *);
GetAsyncKeyState_t _pfnGetAsyncKeyState = GetAsyncKeyState;
GetCursorPos_t _pfnGetCursorPos = GetCursorPos;
SetCursorPos_t _pfnSetCursorPos = SetCursorPos;
ClipCursor_t _pfnClipCursor = ClipCursor;

volatile LONG _slRawX = 0;
volatile LONG _slRawY = 0;
volatile LONG _slRawEvents = 0;
volatile LONG _bBlocked = FALSE;
RECT _rcGameClip = {};
bool _bGameClipValid = false;
#if AF_TEST_AIDS
bool _bLogMouse = false;                       // AF_LOG_MOUSE=1: log each tick's movement
#else
constexpr bool _bLogMouse = false;
#endif

// Guards the mouse movement on its way into the game (see "Low-latency mouse look").
CRITICAL_SECTION _csLook;
float _fInFlightX = 0.0f;                      // axis values GetInput() handed over, not yet in an action
float _fInFlightY = 0.0f;
int _ctActionsThisTick = 0;                    // CreateAction() calls since the last GetInput()
int _ctActionsLastTick = 0;

char *Input() { return _ppInput != nullptr ? *_ppInput : nullptr; }

bool InstallMouseLook();

bool ShouldCaptureMouse()
{
  if (_bBlocked || !IsGameInputEnabled()) return false;
  HWND hwndGame = GetGameWindow();
  return hwndGame != nullptr && GetForegroundWindow() == hwndGame;
}

LRESULT CALLBACK RawInputWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_INPUT) {
    RAWINPUT ri;
    UINT cb = sizeof(ri);
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &ri, &cb, sizeof(RAWINPUTHEADER)) != (UINT)-1
     && ri.header.dwType == RIM_TYPEMOUSE) {
      InterlockedIncrement(&_slRawEvents);
      // absolute devices (tablets, remote desktop) don't give movement counts
      if (!(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) && ShouldCaptureMouse()) {
        InterlockedExchangeAdd(&_slRawX, ri.data.mouse.lLastX);
        InterlockedExchangeAdd(&_slRawY, ri.data.mouse.lLastY);
      }
      if ((ri.data.mouse.usButtonFlags & RI_MOUSE_WHEEL) && ShouldCaptureMouse()) {
        AddMouseWheel((SHORT)ri.data.mouse.usButtonData);
      }
    }
  }
  return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

DWORD WINAPI RawInputThread(LPVOID)
{
  WNDCLASSA wc = {};
  wc.lpfnWndProc = RawInputWndProc;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = "AlphaFixes_RawInput";
  RegisterClassA(&wc);
  HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
  if (hwnd == nullptr) {
    Log("Raw input: cannot create window (%lu)", GetLastError());
    return 0;
  }
  // INPUTSINK: also while the game window has focus but this hidden window doesn't
  RAWINPUTDEVICE rid = { 0x01, 0x02, RIDEV_INPUTSINK, hwnd };   // generic desktop, mouse
  if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
    Log("Raw input: RegisterRawInputDevices failed (%lu)", GetLastError());
    return 0;
  }
  Log("Raw input: listening");
  MSG msg;
  while (GetMessageA(&msg, nullptr, 0, 0) > 0) DispatchMessageA(&msg);
  return 0;
}

void ReplaceAxis(char *pInput, size_t iRel, size_t iAbs, float fNew)
{
  float *pfRel = (float *)(pInput + iRel);
  float *pfAbs = (float *)(pInput + iAbs);
  *pfAbs += fNew - *pfRel;     // GetInput() already added its own value
  *pfRel = fNew;
}

// void CInput::GetInput(void) -- runs on the engine's timer thread
void __fastcall GetInput_Hook(char *pInput, void *pvEdx)
{
  _pfnGetInput(pInput, pvEdx);

  EnterCriticalSection(&_csLook);
  LONG slX = InterlockedExchange(&_slRawX, 0);
  LONG slY = InterlockedExchange(&_slRawY, 0);
  float fX, fY;
  float fCursorX = *(float *)(pInput + INP_MOUSEX_REL);
  float fCursorY = *(float *)(pInput + INP_MOUSEY_REL);
  if (g_cfg.bRawInput) {
    fX = (float)slX;
    fY = (float)slY;
  } else {
    fX = fCursorX;
    fY = fCursorY;
  }
  if (_bLogMouse && (slX != 0 || slY != 0 || fCursorX != 0.0f || fCursorY != 0.0f)) {
    Log("Mouse tick: raw %ld %ld, cursor %.0f %.0f", slX, slY, fCursorX, fCursorY);
  }
  fX *= g_cfg.fSensitivity;
  fY *= g_cfg.fSensitivity * g_cfg.fVerticalRatio;
  ReplaceAxis(pInput, INP_MOUSEX_REL, INP_MOUSEX_ABS, fX);
  ReplaceAxis(pInput, INP_MOUSEY_REL, INP_MOUSEY_ABS, fY);
  // until CControls::CreateAction() turns them into the player's action
  _fInFlightX = fX;
  _fInFlightY = fY;
  _ctActionsLastTick = _ctActionsThisTick;
  _ctActionsThisTick = 0;
  LeaveCriticalSection(&_csLook);
}

// While the overlay is open the engine must see no keys and no mouse movement.
// These replace Engine.dll's imports, which only CInput uses.
SHORT WINAPI GetAsyncKeyState_Hook(int vKey)
{
  if (_bBlocked) return 0;
  return _pfnGetAsyncKeyState(vKey);
}

BOOL WINAPI GetCursorPos_Hook(LPPOINT ppt)
{
  char *pInput = Input();
  if (_bBlocked && pInput != nullptr && ppt != nullptr) {
    ppt->x = *(LONG *)(pInput + INP_CENTER_X);
    ppt->y = *(LONG *)(pInput + INP_CENTER_Y);
    return TRUE;
  }
  return _pfnGetCursorPos(ppt);
}

BOOL WINAPI SetCursorPos_Hook(int x, int y)
{
  if (_bBlocked) return TRUE;
  return _pfnSetCursorPos(x, y);
}

BOOL WINAPI ClipCursor_Hook(const RECT *prc)
{
  // EnableInput() clips to the game window before it sets inp_bInputEnabled;
  // DisableInput() restores the previous clip while it is still set.
  if (prc != nullptr && !IsGameInputEnabled()) {
    _rcGameClip = *prc;
    _bGameClipValid = true;
  }
  if (_bBlocked) return TRUE;
  return _pfnClipCursor(prc);
}

}  // namespace

bool IsGameInputEnabled()
{
  char *pInput = Input();
  return pInput != nullptr && *(int *)(pInput + INP_ENABLED) != 0;
}

void SetInputBlocked(bool bBlocked)
{
  if ((_bBlocked != FALSE) == bBlocked) return;
  if (bBlocked) {
    InterlockedExchange(&_bBlocked, TRUE);
    _pfnClipCursor(nullptr);
  } else {
    InterlockedExchange(&_slRawX, 0);
    InterlockedExchange(&_slRawY, 0);
    char *pInput = Input();
    if (IsGameInputEnabled()) {
      // back to the game: clip and center the cursor again, as EnableInput() did
      if (_bGameClipValid) _pfnClipCursor(&_rcGameClip);
      _pfnSetCursorPos(*(LONG *)(pInput + INP_CENTER_X), *(LONG *)(pInput + INP_CENTER_Y));
    }
    InterlockedExchange(&_bBlocked, FALSE);
  }
}

long PeekRawMouseCount() { return _slRawEvents; }

bool InstallMouseFixes()
{
  _ppInput = (char **)EngineProc("?_pInput@@3PAVCInput@@A");
  if (_ppInput == nullptr) return false;
  InitializeCriticalSection(&_csLook);
#if AF_TEST_AIDS
  char strEnv[8];
  _bLogMouse = GetEnvironmentVariableA("AF_LOG_MOUSE", strEnv, sizeof(strEnv)) > 0 && strEnv[0] == '1';
#endif
  if (!HookFunction(EngineProc("?GetInput@CInput@@QAEXXZ"), (void *)GetInput_Hook, (void **)&_pfnGetInput,
                    "CInput::GetInput")) {
    return false;
  }

  HMODULE hEngine = GetModuleHandleA("Engine.dll");
  if (void *p = HookImport(hEngine, "USER32.dll", "GetAsyncKeyState", (void *)GetAsyncKeyState_Hook)) _pfnGetAsyncKeyState = (GetAsyncKeyState_t)p;
  if (void *p = HookImport(hEngine, "USER32.dll", "GetCursorPos", (void *)GetCursorPos_Hook)) _pfnGetCursorPos = (GetCursorPos_t)p;
  if (void *p = HookImport(hEngine, "USER32.dll", "SetCursorPos", (void *)SetCursorPos_Hook)) _pfnSetCursorPos = (SetCursorPos_t)p;
  if (void *p = HookImport(hEngine, "USER32.dll", "ClipCursor", (void *)ClipCursor_Hook)) _pfnClipCursor = (ClipCursor_t)p;

  HANDLE hThread = CreateThread(nullptr, 0, RawInputThread, nullptr, 0, nullptr);
  if (hThread != nullptr) CloseHandle(hThread);
  return InstallMouseLook();
}

// ---------------------------------------------------------------------------
// Low-latency mouse look
//
// The game turns the player only on its 20 Hz ticks, and draws the player's
// view interpolated between the last two ticks.  A mouse movement waits for the
// next GetInput() (up to 50 ms), becomes an action that the next game tick
// applies, and then is shown spread over the following 50 ms of interpolation.
// The view trails the mouse by 50-100 ms, which feels floaty.
//
// The view drawn on screen gets everything the game hasn't applied or shown
// yet, added to its heading and pitch:
//
//  1. the part of the last tick's turn the interpolation hasn't shown yet,
//  2. actions built but not yet applied by a game tick,
//  3. movement GetInput() has taken but CreateAction() hasn't used yet,
//  4. Raw Input counts GetInput() hasn't taken yet.
//
// The sum is where the player will be looking once the game catches up, so the
// view follows the mouse at once and doesn't move again when the ticks arrive.
// Only the drawn view changes: aiming, shooting and everything else the game
// computes still use the game's own orientation, as do demos and network games.
//
// (2) is a queue: CControls::CreateAction() (timer thread) adds each action's
// turn rates; CPlayer::ApplyAction() (main thread) removes the matching entry
// when it applies an action to the viewed player.  Actions travel through the
// engine as plain 48-byte copies, so the values match exactly.  For (3) and
// (4) mouse movement becomes a turn rate the way CreateAction() does it: axis
// value times the influence of the axis action it is mapped to, times 10 in
// Entities.dll's ComposeActionPacket().  Entries older than 300 ms are dropped: they were
// not applied (pause, level change).  Split screen, death, third-person and
// camera views are drawn as the game draws them.

namespace {

// Entities.dll (preferred base)
constexpr uintptr_t ENT_APPLYACTION = 0x6020C9C0;      // CPlayer::ApplyAction(const CPlayerAction &, FLOAT)
constexpr uintptr_t ENT_GETVIEWPLACEMENT = 0x6020DC10; // CPlayer::GetViewPlacement(CPlacement3D &), only for drawing

// entity layout (alpha build)
constexpr size_t EN_RENDERTYPE = 0x4;                  // 3: model in the world
constexpr size_t EN_FLAGS = 0x10;                      // ENF_ALIVE = 8
constexpr size_t EN_PLACEMENT_ANGLES = 0x28;           // en_plPlacement (+0x1C): heading, pitch, banking
constexpr size_t EN_LASTPLACEMENT_ANGLES = 0x17C;      // en_plLastPlacement (+0x170)
constexpr size_t PL_VIEW_ANGLES = 0x2D0;               // eye placement relative to the body (+0x2C4)
constexpr size_t PL_LASTVIEW_ANGLES = 0x2E8;           // the same before the last action (+0x2DC)
constexpr size_t PL_VIEWMODE = 0x330;                  // 0: first person
// CPlayerAction: pa_aRotation (heading, pitch, banking) in degrees per second
constexpr size_t PA_ROTATION = 0xC;
// CControls: axis actions of 0x14 bytes from +0x10: axis, sensitivity, invert, relative, influence
constexpr size_t CTL_AXISACTIONS = 0x10;
constexpr size_t CTL_AXISACTION_SIZE = 0x14;
constexpr int AXIS_ACTION_PITCH = 3;                   // pa_aRotation(2) = +value * influence
constexpr int AXIS_ACTION_HEADING = 4;                 // pa_aRotation(1) = -value * influence
constexpr int AXIS_MOUSE_X = 1;
constexpr int AXIS_MOUSE_Y = 2;
constexpr float ROTATION_SCALE = 10.0f;                // Entities.dll ComposeActionPacket(): pa_aRotation *= 10
constexpr size_t TIMER_LERPFACTOR = 0x20;              // CTimer: interpolation factor between ticks
constexpr DWORD MAX_ACTION_AGE_MS = 300;

struct QueuedTurn { float fHeading, fPitch; DWORD dwTime; };

using CreateAction_t = void(__fastcall *)(char *pControls, void *pvEdx, char *pAction);
using ApplyAction_t = void(__fastcall *)(char *pen, void *pvEdx, const char *pAction, float fLatency);
using GetViewPlacement_t = void(__fastcall *)(char *pen, void *pvEdx, float *ppl);
using IsPaused_t = BOOL(__fastcall *)(void *pNetwork, void *pvEdx);

CreateAction_t _pfnCreateAction = nullptr;
ApplyAction_t _pfnApplyAction = nullptr;
GetViewPlacement_t _pfnGetViewPlacement = nullptr;
IsPaused_t _pfnIsPaused = nullptr;
void **_ppNetwork = nullptr;
char **_ppTimer = nullptr;
float _fTickQuantum = 0.05f;

std::deque<QueuedTurn> _aqTurns;                       // actions built, not yet applied
// turn rate (degrees per second) per unit of mouse X/Y axis value, from the controls
float _fHeadingPerX = 0.0f, _fHeadingPerY = 0.0f;
float _fPitchPerX = 0.0f, _fPitchPerY = 0.0f;
char *_penViewer = nullptr;                            // the player whose view is drawn

float NormalizeAngle(float f)
{
  f = fmodf(f + 180.0f, 360.0f);
  if (f < 0.0f) f += 360.0f;
  return f - 180.0f;
}

float ClampAbs(float f, float fLimit) { return f < -fLimit ? -fLimit : (f > fLimit ? fLimit : f); }

void ReadAxisMapping(const char *pControls, int iAxisAction, float fSign, float &fPerX, float &fPerY)
{
  const char *paa = pControls + CTL_AXISACTIONS + CTL_AXISACTION_SIZE * iAxisAction;
  int iAxis = *(const int *)paa;
  bool bRelative = *(const int *)(paa + 0xC) != 0;
  float fInfluence = *(const float *)(paa + 0x10);
  // an absolute (joystick-style) mapping turns by position, not movement: no prediction
  fPerX = (bRelative && iAxis == AXIS_MOUSE_X) ? fSign * fInfluence * ROTATION_SCALE : 0.0f;
  fPerY = (bRelative && iAxis == AXIS_MOUSE_Y) ? fSign * fInfluence * ROTATION_SCALE : 0.0f;
}

// void CControls::CreateAction(CPlayerAction &pa) -- timer thread, once per local player and tick
void __fastcall CreateAction_Hook(char *pControls, void *pvEdx, char *pAction)
{
  _pfnCreateAction(pControls, pvEdx, pAction);
  PutWheelInAction(pAction);

  EnterCriticalSection(&_csLook);
  ReadAxisMapping(pControls, AXIS_ACTION_HEADING, -1.0f, _fHeadingPerX, _fHeadingPerY);
  ReadAxisMapping(pControls, AXIS_ACTION_PITCH, 1.0f, _fPitchPerX, _fPitchPerY);
  const float *pfRotation = (const float *)(pAction + PA_ROTATION);
  _aqTurns.push_back({ pfRotation[0], pfRotation[1], GetTickCount() });
  if (_aqTurns.size() > 32) _aqTurns.pop_front();
  _fInFlightX = 0.0f;
  _fInFlightY = 0.0f;
  _ctActionsThisTick++;
  LeaveCriticalSection(&_csLook);
}

bool SameBits(float f1, float f2) { return memcmp(&f1, &f2, sizeof(float)) == 0; }

// void CPlayer::ApplyAction(const CPlayerAction &pa, FLOAT tmLatency) -- main thread, game ticks
void __fastcall ApplyAction_Hook(char *pen, void *pvEdx, const char *pAction, float fLatency)
{
  if (pen == _penViewer) {
    const float *pfRotation = (const float *)(pAction + PA_ROTATION);
    EnterCriticalSection(&_csLook);
    // A turning action is matched anywhere in the queue: its values are as
    // good as unique, and the entries before it were lost or applied unseen.
    // Still actions (no turn) are everywhere, so one only removes the front
    // entry, and only a still one.
    bool bStill = pfRotation[0] == 0.0f && pfRotation[1] == 0.0f;
    size_t ctSearch = bStill ? std::min<size_t>(1, _aqTurns.size()) : _aqTurns.size();
    for (size_t i = 0; i < ctSearch; i++) {
      if (SameBits(_aqTurns[i].fHeading, pfRotation[0]) && SameBits(_aqTurns[i].fPitch, pfRotation[1])) {
        _aqTurns.erase(_aqTurns.begin(), _aqTurns.begin() + i + 1);
        break;
      }
    }
    LeaveCriticalSection(&_csLook);
  }
  _pfnApplyAction(pen, pvEdx, pAction, fLatency);
}

bool IsPaused()
{
  return _pfnIsPaused != nullptr && _ppNetwork != nullptr && *_ppNetwork != nullptr
      && _pfnIsPaused(*_ppNetwork, nullptr) != 0;
}

// Turn (degrees) the game will still apply: queued actions plus mouse movement not in an action yet.
void PendingTurn(float &fHeading, float &fPitch)
{
  EnterCriticalSection(&_csLook);
  DWORD dwNow = GetTickCount();
  while (!_aqTurns.empty() && dwNow - _aqTurns.front().dwTime > MAX_ACTION_AGE_MS) _aqTurns.pop_front();
  float fRateH = 0.0f, fRateP = 0.0f;
  for (const auto &t : _aqTurns) {
    fRateH += t.fHeading;
    fRateP += t.fPitch;
  }
  float fX = _fInFlightX, fY = _fInFlightY;
  if (g_cfg.bRawInput) {
    fX += (float)_slRawX * g_cfg.fSensitivity;
    fY += (float)_slRawY * g_cfg.fSensitivity * g_cfg.fVerticalRatio;
  }
  fRateH += _fHeadingPerX * fX + _fHeadingPerY * fY;
  fRateP += _fPitchPerX * fX + _fPitchPerY * fY;
  LeaveCriticalSection(&_csLook);
  fHeading = fRateH * _fTickQuantum;
  fPitch = fRateP * _fTickQuantum;
}

// void CPlayer::GetViewPlacement(CPlacement3D &pl) -- called only by CPlayer::RenderGameView
void __fastcall GetViewPlacement_Hook(char *pen, void *pvEdx, float *ppl)
{
  _pfnGetViewPlacement(pen, pvEdx, ppl);
  _penViewer = pen;
  if (!g_cfg.bLowLatencyLook || _ppTimer == nullptr || *_ppTimer == nullptr) return;
  if (*(int *)(pen + EN_RENDERTYPE) != 3 || !(*(ULONG *)(pen + EN_FLAGS) & 8) || *(int *)(pen + PL_VIEWMODE) != 0) return;
  if (_ctActionsLastTick > 1 || IsPaused()) return;    // split screen: the mouse isn't the only one turning

  // (1) what the interpolation hasn't shown yet of the last tick: body plus eye
  float fLerp = *(float *)(*_ppTimer + TIMER_LERPFACTOR);
  fLerp = fLerp < 0.0f ? 0.0f : (fLerp > 1.0f ? 1.0f : fLerp);
  const float *pfBody = (const float *)(pen + EN_PLACEMENT_ANGLES);
  const float *pfBodyLast = (const float *)(pen + EN_LASTPLACEMENT_ANGLES);
  const float *pfEye = (const float *)(pen + PL_VIEW_ANGLES);
  const float *pfEyeLast = (const float *)(pen + PL_LASTVIEW_ANGLES);
  float fRestH = (1.0f - fLerp) * (NormalizeAngle(pfBody[0] - pfBodyLast[0]) + NormalizeAngle(pfEye[0] - pfEyeLast[0]));
  float fRestP = (1.0f - fLerp) * (NormalizeAngle(pfBody[1] - pfBodyLast[1]) + (pfEye[1] - pfEyeLast[1]));

  // (2) to (4)
  float fPendingH, fPendingP;
  PendingTurn(fPendingH, fPendingP);

  float *pfAngles = ppl + 3;   // CPlacement3D: position, then heading, pitch, banking
  float fOldH = pfAngles[0], fOldP = pfAngles[1];
  pfAngles[0] = NormalizeAngle(pfAngles[0] + ClampAbs(fRestH + fPendingH, 90.0f));
  pfAngles[1] = ClampAbs(pfAngles[1] + ClampAbs(fRestP + fPendingP, 90.0f), 90.0f);   // the game limits pitch to 90
  if (_bLogMouse) {
    LARGE_INTEGER ll, llFreq;
    QueryPerformanceCounter(&ll);
    QueryPerformanceFrequency(&llFreq);
    Log("View %.1f ms: heading %.2f -> %.2f (lerp %.2f, rest %.2f, pending %.2f), pitch %.2f -> %.2f",
        ll.QuadPart * 1000.0 / llFreq.QuadPart, fOldH, pfAngles[0], fLerp, fRestH, fPendingH, fOldP, pfAngles[1]);
  }
}

bool InstallMouseLook()
{
  _ppTimer = (char **)EngineProc("?_pTimer@@3PAVCTimer@@A");
  _ppNetwork = (void **)EngineProc("?_pNetwork@@3PAVCNetworkLibrary@@A");
  _pfnIsPaused = (IsPaused_t)EngineProc("?IsPaused@CNetworkLibrary@@QAEHXZ");
  if (const float *pf = (const float *)EngineProc("?TickQuantum@CTimer@@2MB")) _fTickQuantum = *pf;
  void *pCreateAction = GetProcAddress(GetModuleHandleA("Game.dll"), "?CreateAction@CControls@@QAEXAAVCPlayerAction@@@Z");
  return HookFunction(pCreateAction, (void *)CreateAction_Hook, (void **)&_pfnCreateAction, "CControls::CreateAction");
}

}  // namespace

void InstallMouseEntitiesHooks(HMODULE)
{
  static const BYTE aubApplyAction[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68 };
  static const BYTE aubGetViewPlacement[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x56, 0x8B, 0xF1, 0x57 };
  uintptr_t ulApplyAction = RebaseEntities(ENT_APPLYACTION);
  uintptr_t ulGetViewPlacement = RebaseEntities(ENT_GETVIEWPLACEMENT);
  if (_pfnCreateAction == nullptr || !CodeMatches(ulApplyAction, aubApplyAction, sizeof(aubApplyAction))
   || !CodeMatches(ulGetViewPlacement, aubGetViewPlacement, sizeof(aubGetViewPlacement))) {
    Log("Low-latency mouse look: code differs, not installed");
    return;
  }
  bool bOk = HookFunction((void *)ulApplyAction, (void *)ApplyAction_Hook, (void **)&_pfnApplyAction, "CPlayer::ApplyAction")
          && HookFunction((void *)ulGetViewPlacement, (void *)GetViewPlacement_Hook, (void **)&_pfnGetViewPlacement,
                          "CPlayer::GetViewPlacement");
  Log("Low-latency mouse look: %s", bOk ? "installed" : "failed");
}
