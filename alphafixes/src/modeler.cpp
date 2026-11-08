// Modeler fixes (Modeler.exe, build of 13 Jan 2000).
//
// The winmm.dll proxy loads AlphaFixes.dll at Modeler.exe's entry point, the
// same way as in the game and the World Editor (Engine.dll imports winmm.dll).
// AF_Init() installs the engine fixes that apply here, most importantly the
// stream fix (streamfix.cpp): on 64-bit Windows the Modeler crashed as soon as
// it read a file (ACCESS_VIOLATION in Engine.dll at 6001171A while the File >
// Open dialog was up), since its loading runs inside window procedures.
//
// Crash at startup when the released Modeler was used before: the toolbar
// layout under HKCU\Software\CroTeam\Modeler is shared with it, the same
// problem as the World Editor's (barstate.cpp).  CMainFrame::OnCreate loads it
// at 0x004127A0 and CMainFrame saves it at 0x00412803.
#include "alphafixes.h"

namespace {

constexpr uintptr_t MOD_LOADBAR_PUSH = 0x004127A0;   // push offset "General"
constexpr uintptr_t MOD_LOADBAR_CALL = 0x004127A7;   // call CFrameWnd::LoadBarState
constexpr uintptr_t MOD_SAVEBAR_PUSH = 0x00412803;   // push offset "General"

const BYTE _aubLoadPushOld[5] = { 0x68, 0x68, 0x9D, 0x43, 0x00 };
const BYTE _aubLoadCallOld[5] = { 0xE8, 0x24, 0x22, 0x01, 0x00 };
const BYTE _aubSavePushOld[5] = { 0x68, 0x70, 0x9D, 0x43, 0x00 };

}  // namespace

void InstallModelerFixes()
{
  bool bBarState = InstallBarStateFix(MOD_LOADBAR_PUSH, MOD_LOADBAR_CALL, MOD_SAVEBAR_PUSH,
                                      _aubLoadPushOld, _aubLoadCallOld, _aubSavePushOld);
  Log("Toolbar layout: %s", bBarState ? "installed" : "code differs, not installed");
}
