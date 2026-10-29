// Sound: the master volume slider, and clipping.
//
// - Options > Audio options has a "Master volume" slider (SeriousSam.exe, a
//   slider gadget at 0x43AA30: minimum +0x3C, maximum +0x40 = 16, position
//   +0x44 = 8), but nothing reads it: the page's start and Apply only handle
//   the sound format.  The engine's CSoundLibrary::SetMasterVolume is called by
//   nobody.  The mod applies the slider to the mixed sound: position / 8 times
//   the level (8, the slider's starting point, is the original level).  The
//   position is kept in the ini and put back into the slider when the page opens.
// - Engine.dll mixes every sound straight into the 16-bit output buffer with
//   saturating adds (MMX paddsw), so loud sounds together clip hard: firing the
//   minigun put 11-15 % of the samples at full scale.  Each sound's volume is
//   also capped (its 16-bit factor saturates), so a master volume given to the
//   mixer can't lower the loud sounds.  So the mod lets the engine mix one sound
//   at a time (MixSound, 0x6003F470) into a cleared buffer of its own, adds it
//   to a 32-bit sum, and makes the output (CopyMixerBuffer, 0x6003F440) from
//   that sum: times the master volume, then through a soft limiter (unchanged
//   below 3/4 of full scale, rounded off above), or with SoftClipping off a
//   plain clamp.  Quiet sounds keep their original level.
#include "alphafixes.h"

#include <cmath>
#include <vector>

namespace {

// SeriousSam.exe (13 Jan 2000)
constexpr uintptr_t EXE_VOLUME_SLIDER = 0x43AA30;     // slider gadget: position at +0x44
constexpr size_t SLIDER_POSITION = 0x44;
constexpr uintptr_t EXE_SLIDER_UPDATE = 0x420270;     // redraws the slider's 3D item for its position
constexpr uintptr_t EXE_AUDIO_START = 0x419940;       // Audio options page: start
// Engine.dll (alpha build)
constexpr uintptr_t ENG_RESET_MIXER = 0x6003F3F0;     // void ResetMixer(SWORD *pBuffer, SLONG slBytes, FLOAT fMasterVolume)
constexpr uintptr_t ENG_MIX_SOUND = 0x6003F470;       // void MixSound(CSoundObject *pso): adds it to the mixer buffer
constexpr uintptr_t ENG_COPY_MIXER = 0x6003F440;      // void CopyMixerBuffer(SLONG slSrcOffset, void *pDest, SLONG slBytes)
constexpr uintptr_t ENG_MIXER_BUFFER = 0x6010D99C;    // SWORD *: where the mixer writes (all its loops read it)

constexpr float KNEE = 24576.0f;                      // 3/4 of full scale

using ResetMixer_t = void(__cdecl *)(short *pBuffer, long slBytes, float fMasterVolume);
using MixSound_t = void(__cdecl *)(void *pso);
using CopyMixer_t = void(__cdecl *)(long slSrcOffset, void *pDest, long slBytes);
using Method_t = void(__fastcall *)(void *pThis, void *pvEdx);
ResetMixer_t _pfnResetMixer = nullptr;
MixSound_t _pfnMixSound = nullptr;
CopyMixer_t _pfnCopyMixer = nullptr;
Method_t _pfnAudioStart = nullptr;

// All mixing runs inside CSoundLibrary::MixSounds, one call at a time.
short *_pswEngineBuffer = nullptr;
std::vector<short> _aswSound;                         // one sound
std::vector<long> _aslSum;                            // all sounds, exact

void __cdecl ResetMixer_Hook(short *pBuffer, long slBytes, float fMasterVolume)
{
  _pfnResetMixer(pBuffer, slBytes, fMasterVolume);
  _pswEngineBuffer = pBuffer;
  size_t ctSamples = slBytes > 0 ? (size_t)slBytes / 2 : 0;
  if (_aswSound.size() < ctSamples) _aswSound.resize(ctSamples);
  _aslSum.assign(ctSamples, 0);
}

void __cdecl MixSound_Hook(void *pso)
{
  size_t ctSamples = _aslSum.size();
  if (ctSamples == 0 || _pswEngineBuffer == nullptr) {
    _pfnMixSound(pso);
    return;
  }
  memset(_aswSound.data(), 0, ctSamples * sizeof(short));
  *(short **)ENG_MIXER_BUFFER = _aswSound.data();
  _pfnMixSound(pso);
  *(short **)ENG_MIXER_BUFFER = _pswEngineBuffer;
  for (size_t i = 0; i < ctSamples; i++) _aslSum[i] += _aswSound[i];
}

short Output(float f)
{
  float a = fabsf(f);
  if (g_cfg.bSoftClipping) {
    if (a > KNEE) a = KNEE + (32767.0f - KNEE) * tanhf((a - KNEE) / (32767.0f - KNEE));
  } else if (a > 32767.0f) {
    a = 32767.0f;
  }
  long sl = (long)(a + 0.5f);
  return (short)(f < 0.0f ? -sl : sl);
}

#if AF_TEST_AIDS
// AF_LOG_SOUND=1: the output's level once a second
bool _bLogSound = false;
double _dSumSquares = 0.0;
long _slSamples = 0, _slFullScale = 0, _slPeak = 0, _slOverKnee = 0;
DWORD _tmLastLog = 0;

void MeasureOutput(const short *ps, long ctSamples)
{
  for (long i = 0; i < ctSamples; i++) {
    long sl = ps[i] < 0 ? -(long)ps[i] : ps[i];
    _dSumSquares += (double)sl * sl;
    if (sl > _slPeak) _slPeak = sl;
    if (sl >= 32767) _slFullScale++;
    if (sl > (long)KNEE) _slOverKnee++;
  }
  _slSamples += ctSamples;
  DWORD tmNow = GetTickCount();
  if (tmNow - _tmLastLog >= 1000 && _slSamples > 0) {
    Log("Sound: rms %.0f, peak %ld, at full scale %ld, over the knee %ld of %ld", sqrt(_dSumSquares / _slSamples), _slPeak,
        _slFullScale, _slOverKnee, _slSamples);
    _dSumSquares = 0.0;
    _slSamples = _slFullScale = _slPeak = _slOverKnee = 0;
    _tmLastLog = tmNow;
  }
}
#endif

void __cdecl CopyMixer_Hook(long slSrcOffset, void *pDest, long slBytes)
{
  size_t iFirst = (size_t)slSrcOffset / 2, ctSamples = (size_t)slBytes / 2;
  if (iFirst + ctSamples > _aslSum.size()) {   // not from a sum of ours
    _pfnCopyMixer(slSrcOffset, pDest, slBytes);
    return;
  }
  float fVolume = g_cfg.iMasterVolume / 8.0f;
  short *pd = (short *)pDest;
  for (size_t i = 0; i < ctSamples; i++) pd[i] = Output(_aslSum[iFirst + i] * fVolume);
#if AF_TEST_AIDS
  if (_bLogSound) MeasureOutput(pd, (long)ctSamples);
#endif
}

void __fastcall AudioStart_Hook(void *pgm, void *pvEdx)
{
  *(int *)(EXE_VOLUME_SLIDER + SLIDER_POSITION) = g_cfg.iMasterVolume;
  ((Method_t)EXE_SLIDER_UPDATE)((void *)EXE_VOLUME_SLIDER, nullptr);
  _pfnAudioStart(pgm, pvEdx);
}

}  // namespace

void InstallSoundFixes()
{
#if AF_TEST_AIDS
  char strEnv[8] = {};
  _bLogSound = GetEnvironmentVariableA("AF_LOG_SOUND", strEnv, sizeof(strEnv)) > 0 && strEnv[0] == '1';
#endif
  // push ebp; mov ebp, esp; mov eax, [ebp + 8]; mov ecx, [_pSoundLibrary]
  static const BYTE aubReset[] = { 0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x8B, 0x0D, 0x10, 0xD9, 0x10, 0x60 };
  // push ebp; mov ebp, esp; sub esp, 0x20; push ebx; push esi; mov esi, [ebp + 8]
  static const BYTE aubMix[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20, 0x53, 0x56, 0x8B, 0x75, 0x08 };
  // push ebp; mov ebp, esp; push esi; push edi; pushf; cld
  static const BYTE aubCopy[] = { 0x55, 0x8B, 0xEC, 0x56, 0x57, 0x66, 0x9C, 0xFC };
  // push esi; mov esi, ecx; call (load the page's values)
  static const BYTE aubAudio[] = { 0x56, 0x8B, 0xF1, 0xE8 };
  bool bOk = CodeMatches(ENG_RESET_MIXER, aubReset, sizeof(aubReset)) && CodeMatches(ENG_MIX_SOUND, aubMix, sizeof(aubMix))
          && CodeMatches(ENG_COPY_MIXER, aubCopy, sizeof(aubCopy)) && CodeMatches(EXE_AUDIO_START, aubAudio, sizeof(aubAudio));
  bOk = bOk && HookFunction((void *)ENG_RESET_MIXER, (void *)ResetMixer_Hook, (void **)&_pfnResetMixer, "ResetMixer")
            && HookFunction((void *)ENG_MIX_SOUND, (void *)MixSound_Hook, (void **)&_pfnMixSound, "MixSound")
            && HookFunction((void *)ENG_COPY_MIXER, (void *)CopyMixer_Hook, (void **)&_pfnCopyMixer, "CopyMixerBuffer")
            && HookFunction((void *)EXE_AUDIO_START, (void *)AudioStart_Hook, (void **)&_pfnAudioStart, "Audio options start");
  Log("Sound fixes: %s", bOk ? "installed" : "code differs, not installed");
}

// Every frame: a moved slider changes the setting, a changed setting (settings
// window) moves the slider.  The game's static set-up puts the slider at 8
// after the mod started, so the first frame puts the saved position back.
void UpdateSound()
{
  static int _iLastPosition = -1;
  int &iPosition = *(int *)(EXE_VOLUME_SLIDER + SLIDER_POSITION);
  if (_iLastPosition >= 0 && iPosition != _iLastPosition && iPosition >= 0 && iPosition <= 16) {
    g_cfg.iMasterVolume = iPosition;
    SaveConfig();
  } else if (iPosition != g_cfg.iMasterVolume) {
    iPosition = g_cfg.iMasterVolume;
  }
  _iLastPosition = iPosition;
}
