/*
 * PatchWorldEditor - patches the Serious Sam alpha WorldEditor.exe (build of
 * 13 Jan 2000) to load SamAlphaEditorFixes.dll at startup.  See
 * SamAlphaEditorFixes.c for why.
 *
 * Usage:  PatchWorldEditor [path\to\WorldEditor.exe]
 *         Without a path it looks for WorldEditor.exe next to the patcher,
 *         then in the current directory.  WorldEditor.exe can also be
 *         dropped onto the patcher in Explorer.
 *
 * The original file is kept as WorldEditor.exe.orig.  Running the patcher on an
 * already patched file does nothing.  An exe patched by an older version, which
 * loads SamAlphaFix.dll (the DLL's earlier name), is updated to load
 * SamAlphaEditorFixes.dll.
 *
 * The result is printed to the console.  If the patcher has a console of its
 * own (started from Explorer), the result is shown in a message box instead,
 * since that console closes as soon as the patcher exits.
 *
 * The exe has an embedded asInvoker manifest (see build.bat).  Without it,
 * Windows treats a 32-bit program with "Patch" in its name as an installer
 * and forces it to run elevated, in a new console window.
 *
 * Changes made to the exe:
 *
 * 1. CWorldEditorApp::Run() at 0x00426CE8:
 *      FF 15 5C 71 45 00   call [__imp_CTStream::EnableStreamHandling]
 *    becomes
 *      E8 13 02 03 00      call 00456F00            ; code cave
 *      90                  nop
 *
 * 2. Code cave in the unused tail of .text, at 0x00456F00:
 *      FF 15 5C 71 45 00   call [__imp_CTStream::EnableStreamHandling]
 *      68 20 6F 45 00      push offset szFixDll      ; "SamAlphaEditorFixes.dll"
 *      FF 15 1C 76 45 00   call [__imp_LoadLibraryA]
 *      C3                  ret
 *    00456F20: "SamAlphaEditorFixes.dll",0
 *
 * 3. .text VirtualSize grows from 0x55EF5 to 0x56000 so the cave is inside
 *    the section (the raw data already covers it).
 *
 * If SamAlphaEditorFixes.dll is missing, LoadLibraryA just fails and the editor
 * runs as before.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "user32.lib")

#define FIX_DLL_NAME    "SamAlphaEditorFixes.dll"
#define OLDER_DLL_NAME  "SamAlphaFix.dll"      /* the DLL's name up to October 2026 */

typedef struct {
  long          lOffset;   /* file offset */
  size_t        ctBytes;
  unsigned char aubOld[32];
  unsigned char aubNew[32];
} Patch;

static const Patch _apatches[] = {
  /* .text VirtualSize (section header #1 at 0x1F0, field at +8) */
  { 0x1F8, 4,
    { 0xF5, 0x5E, 0x05, 0x00 },
    { 0x00, 0x60, 0x05, 0x00 } },
  /* hook in CWorldEditorApp::Run() */
  { 0x26CE8, 6,
    { 0xFF, 0x15, 0x5C, 0x71, 0x45, 0x00 },
    { 0xE8, 0x13, 0x02, 0x03, 0x00, 0x90 } },
  /* code cave */
  { 0x56F00, 18,
    { 0 },
    { 0xFF, 0x15, 0x5C, 0x71, 0x45, 0x00,
      0x68, 0x20, 0x6F, 0x45, 0x00,
      0xFF, 0x15, 0x1C, 0x76, 0x45, 0x00,
      0xC3 } },
  /* DLL name (last entry, see IsOlderPatch) */
  { 0x56F20, 24,
    { 0 },
    FIX_DLL_NAME },
};
#define CT_PATCHES (sizeof(_apatches) / sizeof(_apatches[0]))

/* the DLL name written by older versions of this patcher, padded like the entry above */
static const unsigned char _aubOlderDllName[24] = OLDER_DLL_NAME;

#define EXPECTED_SIZE      606208L
#define TIMESTAMP_OFFSET   0x100L       /* PE header (0xF8) + 8 */
static const unsigned char _aubTimestamp[4] = { 0x2A, 0xDB, 0x7D, 0x38 };  /* 0x387DDB2A */

static int Matches(const unsigned char *pubFile, const unsigned char *pub, size_t ct)
{
  return memcmp(pubFile, pub, ct) == 0;
}

static BOOL FileExists(const char *strFile)
{
  DWORD dwAttributes = GetFileAttributesA(strFile);
  return dwAttributes != INVALID_FILE_ATTRIBUTES && !(dwAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

/* Everything the patcher reports goes into one text, shown when it finishes. */
static char _strReport[4096];

static void Report(const char *strFormat, ...)
{
  size_t ctUsed = strlen(_strReport);
  va_list args;
  va_start(args, strFormat);
  _vsnprintf(_strReport + ctUsed, sizeof(_strReport) - ctUsed - 1, strFormat, args);
  va_end(args);
  _strReport[sizeof(_strReport) - 1] = 0;
}

/* TRUE when no other process shares our console, meaning Windows made it just
   for us (started from Explorer) and it will vanish as soon as we exit. */
static BOOL HasOwnConsole(void)
{
  DWORD adwProcesses[2];
  return GetConsoleProcessList(adwProcesses, 2) <= 1;
}

static int Finish(int iExitCode)
{
  FILE *f = (iExitCode == 0) ? stdout : stderr;
  fputs(_strReport, f);
  fflush(f);
  if (HasOwnConsole()) {
    MessageBoxA(NULL, _strReport, (iExitCode == 0) ? "Patch WorldEditor" : "Patch WorldEditor - error",
      MB_OK | MB_SETFOREGROUND | ((iExitCode == 0) ? MB_ICONINFORMATION : MB_ICONERROR));
  }
  return iExitCode;
}

/* Default target: WorldEditor.exe next to the patcher, else in the current directory. */
static void FindDefaultTarget(char *strFile, DWORD ctFile)
{
  char strExe[MAX_PATH];
  char *pchSlash;
  DWORD ctExe = GetModuleFileNameA(NULL, strExe, sizeof(strExe));

  if (ctExe > 0 && ctExe < sizeof(strExe)) {
    pchSlash = strrchr(strExe, '\\');
    if (pchSlash != NULL) {
      pchSlash[1] = 0;
      _snprintf(strFile, ctFile - 1, "%sWorldEditor.exe", strExe);
      strFile[ctFile - 1] = 0;
      if (FileExists(strFile)) return;
    }
  }
  if (GetFullPathNameA("WorldEditor.exe", ctFile, strFile, NULL) == 0) {
    lstrcpynA(strFile, "WorldEditor.exe", ctFile);
  }
}

/* Remind the user about the DLL if it isn't next to the exe yet. */
static void ReportDllStatus(const char *strFile)
{
  char strDll[MAX_PATH];
  char *pchSlash;

  lstrcpynA(strDll, strFile, sizeof(strDll));
  pchSlash = strrchr(strDll, '\\');
  if (pchSlash != NULL) pchSlash[1] = 0;
  else strDll[0] = 0;
  if ((size_t)lstrlenA(strDll) + sizeof(FIX_DLL_NAME) > sizeof(strDll)) return;
  lstrcatA(strDll, FIX_DLL_NAME);

  if (FileExists(strDll)) {
    Report("\n" FIX_DLL_NAME " is in place, the editor is ready to use.\n");
  } else {
    Report("\n" FIX_DLL_NAME " is not there yet. Copy it to\n'%s'\n"
           "or the fixes won't be active.\n", strDll);
  }

  /* the old DLL is no longer loaded */
  pchSlash = strrchr(strDll, '\\');
  if (pchSlash != NULL) {
    pchSlash[1] = 0;
    lstrcatA(strDll, OLDER_DLL_NAME);
    if (FileExists(strDll)) Report("\nThe old " OLDER_DLL_NAME " is not used any more, you can delete it.\n");
  }
}

/* Patched by an older version: everything in place except the DLL name. */
static int IsOlderPatch(const unsigned char *pubFile)
{
  size_t i;
  for (i = 0; i + 1 < CT_PATCHES; i++) {
    const Patch *pp = &_apatches[i];
    if (!Matches(pubFile + pp->lOffset, pp->aubNew, pp->ctBytes)) return 0;
  }
  return Matches(pubFile + _apatches[CT_PATCHES - 1].lOffset, _aubOlderDllName, sizeof(_aubOlderDllName));
}

int main(int argc, char *argv[])
{
  char strFile[MAX_PATH];
  char strBackup[MAX_PATH + 8];
  unsigned char *pubFile;
  FILE *f;
  long slSize;
  size_t i;
  int bAllOld = 1, bAllNew = 1, bOlderPatch;

  if (argc > 1) {
    if (GetFullPathNameA(argv[1], sizeof(strFile), strFile, NULL) == 0) {
      lstrcpynA(strFile, argv[1], sizeof(strFile));
    }
  } else {
    FindDefaultTarget(strFile, sizeof(strFile));
  }

  f = fopen(strFile, "rb");
  if (f == NULL) {
    Report("Cannot open\n'%s':\n%s.\n\n", strFile, strerror(errno));
    Report("Put the patcher in the game's Bin folder next to WorldEditor.exe and run it there, "
           "or drop WorldEditor.exe onto it.\n");
    return Finish(1);
  }
  fseek(f, 0, SEEK_END);
  slSize = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (slSize != EXPECTED_SIZE) {
    fclose(f);
    Report("'%s' is %ld bytes, expected %ld.\n\nThis is not the WorldEditor.exe from the Serious Sam alpha "
           "(13 Jan 2000), nothing was changed.\n", strFile, slSize, EXPECTED_SIZE);
    return Finish(1);
  }
  pubFile = (unsigned char *)malloc(slSize);
  if (pubFile == NULL || fread(pubFile, 1, slSize, f) != (size_t)slSize) {
    fclose(f);
    Report("Cannot read '%s'.\n", strFile);
    return Finish(1);
  }
  fclose(f);

  if (!Matches(pubFile + TIMESTAMP_OFFSET, _aubTimestamp, 4)) {
    Report("'%s' has an unexpected timestamp.\n\nThis is not the WorldEditor.exe from the Serious Sam alpha "
           "(13 Jan 2000), nothing was changed.\n", strFile);
    return Finish(1);
  }

  for (i = 0; i < CT_PATCHES; i++) {
    const Patch *pp = &_apatches[i];
    if (!Matches(pubFile + pp->lOffset, pp->aubOld, pp->ctBytes)) bAllOld = 0;
    if (!Matches(pubFile + pp->lOffset, pp->aubNew, pp->ctBytes)) bAllNew = 0;
  }
  if (bAllNew) {
    Report("'%s' is already patched, nothing to do.\n", strFile);
    ReportDllStatus(strFile);
    return Finish(0);
  }
  bOlderPatch = IsOlderPatch(pubFile);
  if (!bAllOld && !bOlderPatch) {
    Report("'%s' does not contain the expected bytes.\n\nIt may have been modified by another patch, "
           "nothing was changed.\n", strFile);
    return Finish(1);
  }

  /* back up the original (an older patch already did, from the unpatched file) */
  _snprintf(strBackup, sizeof(strBackup) - 1, "%s.orig", strFile);
  strBackup[sizeof(strBackup) - 1] = 0;
  if (bOlderPatch) {
    /* nothing to back up */
  } else if (FileExists(strBackup)) {
    Report("Backup '%s' already exists, leaving it as is.\n", strBackup);
  } else {
    f = fopen(strBackup, "wb");
    if (f == NULL || fwrite(pubFile, 1, slSize, f) != (size_t)slSize || fclose(f) != 0) {
      Report("Cannot write backup\n'%s':\n%s.\n\nNothing was changed. If the game is installed in "
             "Program Files, run the patcher as administrator.\n", strBackup, strerror(errno));
      return Finish(1);
    }
    Report("Original saved as '%s'.\n", strBackup);
  }

  for (i = 0; i < CT_PATCHES; i++) {
    const Patch *pp = &_apatches[i];
    memcpy(pubFile + pp->lOffset, pp->aubNew, pp->ctBytes);
  }

  f = fopen(strFile, "wb");
  if (f == NULL) {
    Report("Cannot write\n'%s':\n%s.\n\nIt was not changed. Close the World Editor if it is running. "
           "If the game is installed in Program Files, run the patcher as administrator.\n",
           strFile, strerror(errno));
    return Finish(1);
  }
  if (fwrite(pubFile, 1, slSize, f) != (size_t)slSize || fclose(f) != 0) {
    Report("Writing '%s' failed.\nRestore it from '%s'.\n", strFile, strBackup);
    return Finish(1);
  }
  free(pubFile);

  if (bOlderPatch) {
    Report("'%s' was patched by an older version. It now loads " FIX_DLL_NAME
           " instead of " OLDER_DLL_NAME ".\n", strFile);
  } else {
    Report("'%s' patched successfully.\n", strFile);
  }
  ReportDllStatus(strFile);
  return Finish(0);
}
