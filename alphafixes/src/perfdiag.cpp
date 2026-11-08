// Performance diagnostics (test builds, AF_LOG_PERF=1): frame times, what long frames spent their time
// on (shadow map mixing, shadow reductions, texture/sound/model loading, file opens, SwapBuffers, the
// frame limiter's Sleep), where the main thread is during a long frame (sampled by a watchdog thread),
// models and textures that take long to draw or bind, whether the drawn game time advances evenly,
// how much sound waveOut has queued, and when the viewed player's actions are created and applied.
#include "alphafixes.h"

#include <mmsystem.h>
#include <tlhelp32.h>
#include <cmath>
#include <unordered_set>
#include <map>

#if AF_TEST_AIDS

namespace {

using Method_t = void(__fastcall *)(void *pThis, void *pvEdx);
using MixLayers_t = void(__fastcall *)(void *psm, void *pvEdx, long iFirstMip, long iLastMip, BOOL bDynamic);
using Read_t = void(__fastcall *)(void *pThis, void *pvEdx, void *pstrm);
using Sleep_t = void(WINAPI *)(DWORD);
using CurrentTick_t = float(__fastcall *)(void *ptm, void *pvEdx);
CurrentTick_t _pfnCurrentTick = nullptr;
// CTFileStream::Open_t(CTFileName fnm, OpenMode om): the file name by value (char *, long)
using Open_t = void(__fastcall *)(void *pstrm, void *pvEdx, const char *str, long lReserved, int iMode);
Open_t _pfnOpen = nullptr;
Read_t _pfnSoundRead = nullptr, _pfnModelRead = nullptr;
double _tmSound = 0.0, _tmModel = 0.0, _tmOpen = 0.0;
int _ctOpen = 0;
char **_ppTimer = nullptr;
// the time the drawn frame shows: last tick + lerp factor * tick length
double _tmLastShown = -1.0, _tmLastReal = 0.0;
int _ctJerks = 0;
double _tmWorstJerk = 0.0;

Method_t _pfnSwapBuffers = nullptr, _pfnReduce = nullptr, _pfnReduceShadows = nullptr;
MixLayers_t _pfnMixLayers = nullptr;
Read_t _pfnTextureRead = nullptr;
Sleep_t _pfnSleep = nullptr;

// sound latency: what waveOut has been given against what it has played
using WaveOutWrite_t = MMRESULT(WINAPI *)(HWAVEOUT, LPWAVEHDR, UINT);
WaveOutWrite_t _pfnWaveOutWrite = nullptr;
volatile LONGLONG _llWaveWritten = 0;
double _tmWaveReport = 0.0;
double _dWaveMin = 1e9, _dWaveMax = 0.0, _dWaveSum = 0.0;
int _ctWave = 0, _ctUnderrun = 0;
DWORD _dwBytesPerSec = 0;

// watchdog: where the main thread is during a long frame
HANDLE _hMainThread = nullptr;
volatile double _tmFrameStart = 0.0;

bool _bOn = false;
DWORD _dwMainThread = 0;
double _dFreq = 1.0;
double Now() { LARGE_INTEGER ll; QueryPerformanceCounter(&ll); return ll.QuadPart * 1000.0 / _dFreq; }

// per frame (main thread)
double _tmLastSwap = 0.0;
double _tmMix = 0.0, _tmTex = 0.0, _tmSleep = 0.0, _tmReduce = 0.0;
int _ctMix = 0, _ctReduce = 0, _ctTex = 0, _ctSleepAsked = 0;
// summary
double _tmSummary = 0.0;
int _ctFrames = 0, _ctLong = 0;
double _tmMixTotal = 0.0;
int _ctMixTotal = 0, _ctReduceTotal = 0;
double _tmWorst = 0.0, _tmTotal = 0.0;

void __fastcall MixLayers_Hook(void *psm, void *pvEdx, long iFirstMip, long iLastMip, BOOL bDynamic)
{
  double t = Now();
  _pfnMixLayers(psm, pvEdx, iFirstMip, iLastMip, bDynamic);
  double dt = Now() - t;
  _tmMix += dt;
  _ctMix++;
  _tmMixTotal += dt;
  _ctMixTotal++;
}

void __fastcall Reduce_Hook(void *psm, void *pvEdx)
{
  _ctReduce++;
  _ctReduceTotal++;
  _pfnReduce(psm, pvEdx);
}

void __fastcall ReduceShadows_Hook(void *pgfx, void *pvEdx)
{
  double t = Now();
  _pfnReduceShadows(pgfx, pvEdx);
  _tmReduce += Now() - t;
}

void __fastcall TextureRead_Hook(void *ptd, void *pvEdx, void *pstrm)
{
  double t = Now();
  _pfnTextureRead(ptd, pvEdx, pstrm);
  _tmTex += Now() - t;
  _ctTex++;
}

void __fastcall Open_Hook(void *pstrm, void *pvEdx, const char *str, long lReserved, int iMode)
{
  double t = Now();
  Log("PERF open %s", str ? str : "?");
  _pfnOpen(pstrm, pvEdx, str, lReserved, iMode);
  _tmOpen += Now() - t;
  _ctOpen++;
}

void __fastcall SoundRead_Hook(void *p, void *pvEdx, void *pstrm)
{
  double t = Now();
  _pfnSoundRead(p, pvEdx, pstrm);
  _tmSound += Now() - t;
}

void __fastcall ModelRead_Hook(void *p, void *pvEdx, void *pstrm)
{
  double t = Now();
  _pfnModelRead(p, pvEdx, pstrm);
  _tmModel += Now() - t;
}

MMRESULT WINAPI WaveOutWrite_Hook(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cb)
{
  // CSoundLibrary's WAVEFORMATEX at +0x40: nAvgBytesPerSec at +0x48
  static char **ppSoundLibrary = (char **)EngineProc("?_pSoundLibrary@@3PAVCSoundLibrary@@A");
  using GetPosition_t = MMRESULT(WINAPI *)(HWAVEOUT, LPMMTIME, UINT);
  static GetPosition_t pfnGetPosition = (GetPosition_t)GetProcAddress(GetModuleHandleA("winmm.dll"), "waveOutGetPosition");
  if (ppSoundLibrary != nullptr && *ppSoundLibrary != nullptr) _dwBytesPerSec = *(DWORD *)(*ppSoundLibrary + 0x48);
  MMTIME mmt = { TIME_BYTES };
  if (_dwBytesPerSec > 0 && pfnGetPosition != nullptr && pfnGetPosition(hwo, &mmt, sizeof(mmt)) == MMSYSERR_NOERROR
   && mmt.wType == TIME_BYTES) {
    double dQueued = (double)(_llWaveWritten - (LONGLONG)mmt.u.cb) * 1000.0 / _dwBytesPerSec;
    if (dQueued <= 0.5) _ctUnderrun++;
    if (dQueued < _dWaveMin) _dWaveMin = dQueued;
    if (dQueued > _dWaveMax) _dWaveMax = dQueued;
    _dWaveSum += dQueued;
    _ctWave++;
  }
  MMRESULT mr = _pfnWaveOutWrite(hwo, pwh, cb);
  _llWaveWritten += pwh->dwBufferLength;
  double t = Now();
  if (t - _tmWaveReport > 5000.0 && _ctWave > 0) {
    Log("PERF sound queued before each write: min %.1f, average %.1f, max %.1f ms; %d underruns", _dWaveMin,
        _dWaveSum / _ctWave, _dWaveMax, _ctUnderrun);
    _tmWaveReport = t;
    _dWaveMin = 1e9; _dWaveMax = _dWaveSum = 0.0; _ctWave = _ctUnderrun = 0;
  }
  return mr;
}

const char *ModuleOf(DWORD dwAddr, DWORD &dwOffset)
{
  static const char *const astr[] = { "Engine.dll", "Entities.dll", "Game.dll", "AlphaFixes.dll", "OPENGL32.dll", "ntdll.dll",
    "KERNELBASE.dll", "KERNEL32.dll", "nvoglv32.dll", "win32u.dll", "gdi32.dll", "gdi32full.dll", "user32.dll", "winmm.dll",
    "wdmaud.drv", "AUDIOSES.DLL", "MMDevAPI.dll", "ucrtbase.dll", "msvcrt.dll" };
  for (const char *str : astr) {
    HMODULE h = GetModuleHandleA(str);
    if (h == nullptr) continue;
    IMAGE_NT_HEADERS *pnt = (IMAGE_NT_HEADERS *)((BYTE *)h + ((IMAGE_DOS_HEADER *)h)->e_lfanew);
    DWORD dwBase = (DWORD)(uintptr_t)h;
    if (dwAddr >= dwBase && dwAddr < dwBase + pnt->OptionalHeader.SizeOfImage) {
      dwOffset = dwAddr - dwBase + (DWORD)pnt->OptionalHeader.ImageBase;   // as at the preferred base
      return str;
    }
  }
  HMODULE hExe = GetModuleHandleA(nullptr);
  IMAGE_NT_HEADERS *pnt = (IMAGE_NT_HEADERS *)((BYTE *)hExe + ((IMAGE_DOS_HEADER *)hExe)->e_lfanew);
  DWORD dwBase = (DWORD)(uintptr_t)hExe;
  dwOffset = dwAddr;
  return (dwAddr >= dwBase && dwAddr < dwBase + pnt->OptionalHeader.SizeOfImage) ? "SeriousSam.exe" : "?";
}

volatile bool _bWatch = true;

DWORD WINAPI Watchdog(LPVOID)
{
  double tmLastSample = 0.0;
  while (_bWatch) {
    Sleep(4);
    double tmStart = _tmFrameStart, t = Now();
    if (tmStart <= 0.0 || t - tmStart < 30.0 || t - tmLastSample < 8.0) continue;
    tmLastSample = t;
    if (SuspendThread(_hMainThread) == (DWORD)-1) continue;
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    char str[600];
    int n = 0;
    if (GetThreadContext(_hMainThread, &ctx)) {
      DWORD dwOff;
      const char *strMod = ModuleOf(ctx.Eip, dwOff);
      n += sprintf_s(str + n, sizeof(str) - n, "%s!%08lX", strMod, dwOff);
      // return addresses on the stack that point into code
      DWORD *pdw = (DWORD *)(uintptr_t)ctx.Esp;
      int ctFound = 0;
      for (int i = 0; i < 2048 && ctFound < 10 && n < 520; i++) {
        DWORD dw;
        __try { dw = pdw[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        strMod = ModuleOf(dw, dwOff);
        if (strMod[0] != '?' && strcmp(strMod, "AlphaFixes.dll") != 0) {
          n += sprintf_s(str + n, sizeof(str) - n, " <%s!%08lX", strMod, dwOff);
          ctFound++;
        }
      }
    }
    ResumeThread(_hMainThread);
    Log("PERF sample %.0f ms into the frame: %s", t - tmStart, str);
    // once per long frame, past 50 ms: every other thread too
    static double _tmAllDone = 0.0;
    if (t - tmStart > 50.0 && tmStart != _tmAllDone) {
      _tmAllDone = tmStart;
      HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
      THREADENTRY32 te = { sizeof(te) };
      DWORD dwSelf = GetCurrentThreadId(), dwPid = GetCurrentProcessId();
      for (BOOL b = Thread32First(hSnap, &te); b; b = Thread32Next(hSnap, &te)) {
        if (te.th32OwnerProcessID != dwPid || te.th32ThreadID == dwSelf || te.th32ThreadID == _dwMainThread) continue;
        HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (hThread == nullptr) continue;
        FILETIME ftC, ftE, ftK, ftU;
        GetThreadTimes(hThread, &ftC, &ftE, &ftK, &ftU);
        if (SuspendThread(hThread) != (DWORD)-1) {
          CONTEXT c = {};
          c.ContextFlags = CONTEXT_CONTROL;
          if (GetThreadContext(hThread, &c)) {
            DWORD dwOff;
            const char *strMod = ModuleOf(c.Eip, dwOff);
            Log("PERF   thread %lu: %s!%08lX, user time %.0f ms", te.th32ThreadID, strMod, dwOff,
                ((ULONGLONG)ftU.dwHighDateTime << 32 | ftU.dwLowDateTime) / 10000.0);
          }
          ResumeThread(hThread);
        }
        CloseHandle(hThread);
      }
      CloseHandle(hSnap);
    }
  }
  return 0;
}

using RenderModel_t = void(__fastcall *)(void *pmo, void *pvEdx, void *prm);
using GetData_t = void *(__fastcall *)(void *pmo, void *pvEdx);
using GetName_t = const char *const *(__fastcall *)(void *pser, void *pvEdx);
using SetAsCurrent_t = void(__fastcall *)(void *ptd, void *pvEdx, long iFrame, float fMipFactor);
RenderModel_t _pfnRenderModel = nullptr;
GetData_t _pfnGetModelData = nullptr;
GetName_t _pfnGetName = nullptr;
SetAsCurrent_t _pfnSetAsCurrent = nullptr;

void __fastcall RenderModel_Hook(void *pmo, void *pvEdx, void *prm)
{
  double t = Now();
  _pfnRenderModel(pmo, pvEdx, prm);
  double dt = Now() - t;
  static std::unordered_set<void *> _setDrawn;
  void *pmd = _pfnGetModelData(pmo, nullptr);
  bool bFirst = _setDrawn.insert(pmd).second;
  if (dt > 20.0 || bFirst) {
    const char *const *pstr = pmd ? _pfnGetName(pmd, nullptr) : nullptr;
    Log("PERF model render%s %.1f ms at %.0f: %s", bFirst ? " (first)" : "", dt, t, pstr && *pstr ? *pstr : "?");
  }
}

void __fastcall SetAsCurrent_Hook(void *ptd, void *pvEdx, long iFrame, float fMipFactor)
{
  double t = Now();
  _pfnSetAsCurrent(ptd, pvEdx, iFrame, fMipFactor);
  double dt = Now() - t;
  if (dt > 10.0) {
    const char *const *pstr = _pfnGetName(ptd, nullptr);
    Log("PERF texture set %.1f ms: %s", dt, pstr && *pstr ? *pstr : "?");
  }
}

void WINAPI Sleep_Hook(DWORD dw)
{
  double t = Now();
  _pfnSleep(dw);
  if (GetCurrentThreadId() == _dwMainThread) {
    _tmSleep += Now() - t;
    _ctSleepAsked += (int)dw;
  }
}

bool _bFinish = false;
using Finish_t = void(WINAPI *)(void);
Finish_t _pfnFinish = nullptr;

void __fastcall SwapBuffers_Hook(void *pvp, void *pvEdx)
{
  if (_bFinish && _pfnFinish != nullptr) {
    // how long the GPU still needed for this frame
    double t = Now();
    _pfnFinish();
    double dt = Now() - t;
    if (dt > 4.0) Log("PERF glFinish %.1f ms at %.0f (CPU part of the frame %.1f ms)", dt, t, t - _tmLastSwap);
  }
  double tStart = Now();
  _pfnSwapBuffers(pvp, pvEdx);
  double tEnd = Now();
  double tmFrame = tEnd - _tmLastSwap;
  if (_tmLastSwap > 0.0) {
    _ctFrames++;
    _tmTotal += tmFrame;
    if (tmFrame > _tmWorst) _tmWorst = tmFrame;
    int ctUp; long long llUp; double dUp;
    TakeGlUploadStats(ctUp, llUp, dUp);
    EndGlFrame(tmFrame > 25.0);
    if (tmFrame > 25.0) {
      _ctLong++;
      Log("PERF   uploads this frame: %d textures, %lld pixels, %.1f ms", ctUp, llUp, dUp);
      Log("PERF long frame %.1f ms at %.0f: swap %.1f, sleep %.1f (asked %d), mix %.1f (%d), reduce %.1f (%d maps), "
          "textures %.1f (%d), sounds %.1f, models %.1f, opens %.1f (%d)", tmFrame, tEnd, tEnd - tStart, _tmSleep,
          _ctSleepAsked, _tmMix, _ctMix, _tmReduce, _ctReduce, _tmTex, _ctTex, _tmSound, _tmModel, _tmOpen, _ctOpen);
    }
  }
  if (tEnd - _tmSummary > 5000.0) {
    if (_ctFrames > 0) {
      Log("PERF %d frames, average %.2f ms, worst %.1f ms, %d over 25 ms; %d jerks, worst %+.1f ms; shadows mixed %d "
          "(%.1f ms), reduced %d", _ctFrames, _tmTotal / _ctFrames, _tmWorst, _ctLong, _ctJerks, _tmWorstJerk,
          _ctMixTotal, _tmMixTotal, _ctReduceTotal);
      _tmMixTotal = 0.0;
      _ctMixTotal = _ctReduceTotal = 0;
    }
    _tmSummary = tEnd;
    _ctFrames = _ctLong = _ctJerks = 0;
    _tmWorstJerk = 0.0;
    _tmWorst = _tmTotal = 0.0;
  }
  // the game time this frame showed, against the real time that passed
  if (_pfnCurrentTick != nullptr && _ppTimer != nullptr && *_ppTimer != nullptr) {
    float fLerp = *(float *)(*_ppTimer + 0x20);
    double tmShown = (_pfnCurrentTick(*_ppTimer, nullptr) - 0.05f + fLerp * 0.05f) * 1000.0;
    if (_tmLastShown >= 0.0) {
      double dShown = tmShown - _tmLastShown, dReal = tStart - _tmLastReal;
      double dJerk = dShown - dReal;
      if (fabs(dJerk) > 6.0 && fabs(dJerk) < 1000.0) {
        _ctJerks++;
        if (fabs(dJerk) > fabs(_tmWorstJerk)) _tmWorstJerk = dJerk;
        Log("PERF jerk at %.0f: shown %+.1f ms in %.1f ms real (lerp %.2f)", tStart, dShown, dReal, fLerp);
      }
    }
    _tmLastShown = tmShown;
    _tmLastReal = tStart;
  }
  _tmLastSwap = tEnd;
  _tmFrameStart = tEnd;
  _tmMix = _tmTex = _tmSleep = _tmReduce = _tmSound = _tmModel = _tmOpen = 0.0;
  _ctOpen = 0;
  _ctMix = _ctReduce = _ctTex = _ctSleepAsked = 0;
}

}  // namespace

void PerfNoteAction(const char *strWhat, const char *pAction)
{
  if (!_bOn) return;
  const float *pf = (const float *)pAction;
  Log("PERF action %s at %.1f: move %.3f %.3f %.3f turn %.4f %.4f buttons %08lX", strWhat, Now(), pf[0], pf[1], pf[2],
      pf[3], pf[4], *(const ULONG *)(pAction + 0x24));
}

void InstallPerfDiag()
{
  char str[8];
  _bOn = GetEnvironmentVariableA("AF_LOG_PERF", str, sizeof(str)) > 0 && str[0] == '1';
  if (!_bOn) return;
  LARGE_INTEGER ll;
  QueryPerformanceFrequency(&ll);
  _dFreq = (double)ll.QuadPart;
  _dwMainThread = GetCurrentThreadId();
  _ppTimer = (char **)EngineProc("?_pTimer@@3PAVCTimer@@A");
  _pfnCurrentTick = (CurrentTick_t)EngineProc("?CurrentTick@CTimer@@QBE?BMXZ");
  HookFunction(EngineProc("?SwapBuffers@CViewPort@@QAEXXZ"), (void *)SwapBuffers_Hook, (void **)&_pfnSwapBuffers, "perf SwapBuffers");
  HookFunction(EngineProc("?MixLayers@CBrushShadowMap@@UAEXJJH@Z"), (void *)MixLayers_Hook, (void **)&_pfnMixLayers, "perf MixLayers");
  HookFunction(EngineProc("?Reduce@CShadowMap@@QAEXXZ"), (void *)Reduce_Hook, (void **)&_pfnReduce, "perf Reduce");
  HookFunction(EngineProc("?ReduceShadows@CGfxLibrary@@QAEXXZ"), (void *)ReduceShadows_Hook, (void **)&_pfnReduceShadows, "perf ReduceShadows");
  HookFunction(EngineProc("?Read_t@CTextureData@@UAEXPAVCTStream@@@Z"), (void *)TextureRead_Hook, (void **)&_pfnTextureRead, "perf texture read");
  HookFunction(EngineProc("?Open_t@CTFileStream@@QAEXVCTFileName@@W4OpenMode@CTStream@@@Z"), (void *)Open_Hook, (void **)&_pfnOpen, "perf open");
  HookFunction(EngineProc("?Read_t@CSoundData@@UAEXPAVCTStream@@@Z"), (void *)SoundRead_Hook, (void **)&_pfnSoundRead, "perf sound read");
  HookFunction(EngineProc("?Read_t@CModelData@@UAEXPAVCTStream@@@Z"), (void *)ModelRead_Hook, (void **)&_pfnModelRead, "perf model read");
  _pfnGetModelData = (GetData_t)EngineProc("?GetData@CModelObject@@QAEPAVCModelData@@XZ");
  _pfnGetName = (GetName_t)EngineProc("?GetName@CSerial@@QAEABVCTFileName@@XZ");
  HookFunction(EngineProc("?RenderModel@CModelObject@@QAEXAAVCRenderModel@@@Z"), (void *)RenderModel_Hook, (void **)&_pfnRenderModel, "perf render model");
  HookFunction(EngineProc("?SetAsCurrent@CTextureData@@QAEXJM@Z"), (void *)SetAsCurrent_Hook, (void **)&_pfnSetAsCurrent, "perf texture set");
  _pfnWaveOutWrite = (WaveOutWrite_t)HookImport(GetModuleHandleA("Engine.dll"), "WINMM.dll", "waveOutWrite", (void *)WaveOutWrite_Hook);
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &_hMainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
  CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
  _pfnSleep = (Sleep_t)HookImport(GetModuleHandleA(nullptr), "KERNEL32.dll", "Sleep", (void *)Sleep_Hook);
  _bFinish = GetEnvironmentVariableA("AF_PERF_FINISH", str, sizeof(str)) > 0 && str[0] == '1';
  _pfnFinish = (Finish_t)GetProcAddress(LoadLibraryA("opengl32.dll"), "glFinish");
  Log("PERF diagnostics on%s", _bFinish ? ", glFinish before each swap" : "");
}

#else

void PerfNoteAction(const char *, const char *) {}
void InstallPerfDiag() {}

#endif
