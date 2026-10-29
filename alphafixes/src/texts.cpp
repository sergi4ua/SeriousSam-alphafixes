// Texts.
//
// - The menu's top line, "Evaluation version for Croteam_Test" (SeriousSam.exe
//   prints "Evaluation version for %s" with the engine's build name), names
//   the mod and its version instead.
// - "Not precached: (0x...)'file'" is printed to the console for every file
//   loaded during play rather than while the level was precaching (Engine.dll
//   0x600A34B0 prints it when the precaching flag at 0x601118AC is off).  On a
//   modern PC those loads go unnoticed, so the message is only noise and is
//   skipped.
#include "alphafixes.h"

namespace {

// SeriousSam.exe (13 Jan 2000)
constexpr uintptr_t EXE_EVALUATION_TEXT = 0x416158;     // push "Evaluation version for %s"
// Engine.dll (alpha build)
constexpr uintptr_t ENG_NOT_PRECACHED_TEST = 0x600A34CF;  // je past the "Not precached" print

const char _strBanner[] = AF_TITLE " " AF_VERSION " by " AF_AUTHOR;

}  // namespace

void InstallTextFixes()
{
  static const BYTE aubPushOld[] = { 0x68, 0x54, 0xF3, 0x42, 0x00 };
  BYTE aubPushNew[5] = { 0x68 };
  uint32_t ulBanner = (uint32_t)(uintptr_t)_strBanner;
  memcpy(aubPushNew + 1, &ulBanner, 4);
  bool bBanner = CodeMatches(EXE_EVALUATION_TEXT, aubPushOld, sizeof(aubPushOld))
              && WriteCode((void *)EXE_EVALUATION_TEXT, aubPushNew, sizeof(aubPushNew));
  Log("Menu banner: %s", bBanner ? _strBanner : "code differs, not changed");

  if (g_cfg.bHideNotPrecached) {
    // test ah, 0x40; je +0x19 -> jmp +0x19
    static const BYTE aubOld[] = { 0xF6, 0xC4, 0x40, 0x74, 0x19 };
    static const BYTE aubNew[] = { 0xF6, 0xC4, 0x40, 0xEB, 0x19 };
    bool bOk = CodeMatches(ENG_NOT_PRECACHED_TEST - 3, aubOld, sizeof(aubOld))
            && WriteCode((void *)(ENG_NOT_PRECACHED_TEST - 3), aubNew, sizeof(aubNew));
    Log("\"Not precached\" messages: %s", bOk ? "hidden" : "code differs, not changed");
  }
}
