// WldConvert: converts Serious Sam: The First Encounter worlds (.wld) to the
// format of the Serious Sam alpha (build of 13 Jan 2000).
//
//   WldConvert [options] <TFE world.wld> <output.wld>
//
// See README.md for what is converted and what is lost.
#include "convert.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <iterator>

namespace {

void Usage()
{
  printf(
    "WldConvert - converts Serious Sam: The First Encounter worlds to the Serious Sam alpha\n"
    "\n"
    "Usage: WldConvert [options] <TFE world.wld> <output.wld>\n"
    "\n"
    "Options:\n"
    "  --game <folder>     the alpha's folder (default: the folder above the one WldConvert.exe is in)\n"
    "  --difficulty <d>    keep only entities active at this difficulty: tourist, easy, normal,\n"
    "                      hard, extreme (default: keep them all, and write their difficulty flags\n"
    "                      to <output>.spawnflags for AlphaFixes' Difficulty setting)\n"
    "  --mode <m>          keep entities active in this game mode: single (default), coop, dm\n"
    "  --all-entities      keep entities whatever their difficulty and game mode flags\n"
    "  --alpha-trigger     triggers become the alpha's Trigger instead of Trigger2\n"
    "  --no-engine         skip the engine pass (entities are then not initialized)\n"
    "  --report <file>     also write the conversion report to this file\n"
    "  --force             overwrite the output file if it exists\n"
    "\n"
    "The engine pass needs WldConvert.exe in the alpha's Bin folder.\n");
}

std::string FullPath(const std::string &str)
{
  char ach[MAX_PATH];
  DWORD ct = GetFullPathNameA(str.c_str(), MAX_PATH, ach, nullptr);
  return (ct > 0 && ct < MAX_PATH) ? std::string(ach) : str;
}

std::string ExeDir()
{
  char ach[MAX_PATH];
  GetModuleFileNameA(nullptr, ach, MAX_PATH);
  std::string str = ach;
  return str.substr(0, str.find_last_of('\\'));
}

bool FileExists(const std::string &str)
{
  DWORD dw = GetFileAttributesA(str.c_str());
  return dw != INVALID_FILE_ATTRIBUTES && !(dw & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadFile(const std::string &strPath, Bytes &ab)
{
  std::ifstream f(strPath, std::ios::binary);
  if (!f) return false;
  ab.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

bool WriteFile(const std::string &strPath, const Bytes &ab)
{
  std::ofstream f(strPath, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write((const char *)ab.data(), ab.size());
  return (bool)f;
}

// path relative to the game folder, or empty if it isn't inside it
std::string RelativeToGame(const std::string &strGame, const std::string &strPath)
{
  std::string strPrefix = ToLower(strGame) + "\\";
  if (ToLower(strPath).compare(0, strPrefix.size(), strPrefix) != 0) return std::string();
  return strPath.substr(strPrefix.size());
}

}  // namespace

int main(int argc, char **argv)
{
  Options opt;
  std::string strIn, strOut, strReport;
  bool bEngine = true, bForce = false;
  uint32_t ulDifficulty = 1u << 1, ulMode = 1u << 16;

  for (int i = 1; i < argc; i++) {
    std::string str = argv[i];
    bool bNext = i + 1 < argc;
    if (str == "--game" && bNext) opt.strGameDir = argv[++i];
    else if (str == "--difficulty" && bNext) {
      std::string strD = ToLower(argv[++i]);
      if (strD == "easy") ulDifficulty = 1u << 0;
      else if (strD == "normal") ulDifficulty = 1u << 1;
      else if (strD == "hard") ulDifficulty = 1u << 2;
      else if (strD == "extreme") ulDifficulty = 1u << 3;
      else if (strD == "tourist") ulDifficulty = 1u << 4;
      else { fprintf(stderr, "unknown difficulty '%s'\n", strD.c_str()); return 1; }
      opt.bDropByDifficulty = true;
    }
    else if (str == "--mode" && bNext) {
      std::string strM = ToLower(argv[++i]);
      if (strM == "single") ulMode = 1u << 16;
      else if (strM == "dm" || strM == "deathmatch") ulMode = 1u << 17;
      else if (strM == "coop" || strM == "cooperative") ulMode = 1u << 18;
      else { fprintf(stderr, "unknown game mode '%s'\n", strM.c_str()); return 1; }
    }
    else if (str == "--all-entities") opt.bAllSpawnFlags = true;
    else if (str == "--alpha-trigger") opt.bAlphaTrigger = true;
    else if (str == "--no-engine") bEngine = false;
    else if (str == "--report" && bNext) strReport = argv[++i];
    else if (str == "--force") bForce = true;
    else if (str == "-h" || str == "--help" || str == "/?") { Usage(); return 0; }
    else if (!str.empty() && str[0] == '-') { fprintf(stderr, "unknown option '%s'\n\n", str.c_str()); Usage(); return 1; }
    else if (strIn.empty()) strIn = str;
    else if (strOut.empty()) strOut = str;
    else { Usage(); return 1; }
  }
  if (strIn.empty() || strOut.empty()) { Usage(); return 1; }
  opt.ulSpawnMask = ulDifficulty | ulMode;

  strIn = FullPath(strIn);
  strOut = FullPath(strOut);
  if (opt.strGameDir.empty()) {
    std::string strExe = ExeDir();
    opt.strGameDir = strExe.substr(0, strExe.find_last_of('\\'));
  }
  opt.strGameDir = FullPath(opt.strGameDir);
  while (!opt.strGameDir.empty() && opt.strGameDir.back() == '\\') opt.strGameDir.pop_back();

  if (ToLower(strIn) == ToLower(strOut)) {
    fprintf(stderr, "The output must be a new file; the input world is never overwritten.\n");
    return 1;
  }
  if (FileExists(strOut) && !bForce) {
    fprintf(stderr, "%s already exists (use --force to overwrite it).\n", strOut.c_str());
    return 1;
  }
  if (!FileExists(opt.strGameDir + "\\Bin\\Engine.dll") || !FileExists(opt.strGameDir + "\\Bin\\Entities.dll")) {
    fprintf(stderr, "%s doesn't look like the alpha's folder (no Bin\\Engine.dll and Bin\\Entities.dll); use --game.\n", opt.strGameDir.c_str());
    return 1;
  }

  Bytes abIn;
  if (!ReadFile(strIn, abIn)) {
    fprintf(stderr, "Cannot read %s\n", strIn.c_str());
    return 1;
  }

  TfeClasses tfe;
  AlphaClasses alpha;
  std::string strError;
  if (!alpha.Load(opt.strGameDir, strError)) {
    fprintf(stderr, "%s\n", strError.c_str());
    return 1;
  }

  Report rep;
  int iExit = 0;
  // the alpha's engine: checks the models and textures, then does the engine pass
  AlphaEngine engine;
  bool bEngineRan = false;
  if (bEngine && ToLower(ExeDir()) != ToLower(opt.strGameDir + "\\Bin")) {
    rep.Add("Engine pass", "skipped: WldConvert.exe must be in " + opt.strGameDir + "\\Bin for it");
    bEngine = false;
  }
  if (bEngine) {
    bEngineRan = true;
    if (engine.Start(opt.strGameDir, strError)) {
      opt.pEngine = &engine;
    } else {
      rep.Add("Engine pass", "FAILED: " + strError);
      bEngine = false;
      iExit = 2;
    }
  }

  World wo;
  OutWorld out;
  try {
    ReadTfeWorld(abIn, tfe, wo, rep);
    ConvertWorld(wo, tfe, alpha, opt, out, rep);
  } catch (const FormatError &e) {
    fprintf(stderr, "%s: %s\n", strIn.c_str(), e.what());
    fflush(stderr);
    if (bEngineRan) TerminateProcess(GetCurrentProcess(), 1);
    return 1;
  }
  Bytes abOut = WriteAlphaWorld(out);
  std::vector<uint32_t> aulSpawnFlags;
  for (const OutEntity &oe : out.aentities) aulSpawnFlags.push_back(oe.ulSpawnFlags);
  std::string strSpawnFlags;
  // the spawn flags file for the world as the converter wrote it (no engine pass)
  auto SpawnFlagsOfOutput = [&]() {
    std::vector<SpawnFlagsEntry> aEntries;
    for (size_t i = 0; i < out.aentities.size(); i++) {
      const OutEntity &oe = out.aentities[i];
      if ((oe.ulSpawnFlags & SPF_ALWAYS) == SPF_ALWAYS) continue;
      aEntries.push_back({ (int32_t)i, oe.pac->strName, { oe.pl.af[0], oe.pl.af[1], oe.pl.af[2] }, oe.ulSpawnFlags });
    }
    return SpawnFlagsText(FileBase(strOut), (int32_t)out.aentities.size(), aEntries);
  };

  if (!bEngine) {
    if (!WriteFile(strOut, abOut)) { fprintf(stderr, "Cannot write %s\n", strOut.c_str()); return 1; }
    strSpawnFlags = SpawnFlagsOfOutput();
    rep.Add("Engine pass", "not done: entities are in the file but not initialized (they don't act in the game);"
                           " a property change in the editor initializes an entity");
  } else {
    // the engine reads and writes files relative to the game folder
    std::string strTag = std::to_string(GetCurrentProcessId());
    CreateDirectoryA((opt.strGameDir + "\\Temp").c_str(), nullptr);
    std::string strRelRaw = "Temp\\WldConvert_" + strTag + "_raw.wld";
    std::string strRelOut = RelativeToGame(opt.strGameDir, strOut);
    bool bCopy = strRelOut.empty();
    if (bCopy) strRelOut = "Temp\\WldConvert_" + strTag + "_out.wld";
    if (!WriteFile(opt.strGameDir + "\\" + strRelRaw, abOut)) {
      fprintf(stderr, "Cannot write %s\\%s\n", opt.strGameDir.c_str(), strRelRaw.c_str());
      return 1;
    }
    engine.CleanUpOnAbort(opt.strGameDir + "\\" + strRelRaw);
    if (bCopy) engine.CleanUpOnAbort(opt.strGameDir + "\\" + strRelOut);
    printf("Converted; now loading it in the alpha's engine to initialize the entities.\n");
    fflush(stdout);
    bool bOK = engine.Reinitialize(strRelRaw, strRelOut, aulSpawnFlags, strSpawnFlags, strError);
    if (bOK && bCopy && !CopyFileA((opt.strGameDir + "\\" + strRelOut).c_str(), strOut.c_str(), FALSE)) {
      bOK = false;
      strError = "cannot copy the result to " + strOut;
    }
    if (bCopy) DeleteFileA((opt.strGameDir + "\\" + strRelOut).c_str());
    DeleteFileA((opt.strGameDir + "\\" + strRelRaw).c_str());
    if (bOK) {
      rep.Add("Engine pass", "done: entities initialized by the alpha's Entities.dll and the world saved by its engine");
    } else {
      // keep what the converter made: the editor still opens it
      WriteFile(strOut, abOut);
      strSpawnFlags = SpawnFlagsOfOutput();
      rep.Add("Engine pass", "FAILED (" + strError + "); the world was written without it");
      iExit = 2;
    }
  }
  {
    // next to the output: "X.wld" -> "X.spawnflags"; an old one is removed when no entity has flags
    std::string strFlagsFile = strOut;
    size_t iDot = strFlagsFile.find_last_of('.'), iSlash = strFlagsFile.find_last_of("\\/");
    if (iDot != std::string::npos && (iSlash == std::string::npos || iDot > iSlash)) strFlagsFile.erase(iDot);
    strFlagsFile += ".spawnflags";
    if (strSpawnFlags.empty()) {
      DeleteFileA(strFlagsFile.c_str());
    } else if (WriteFile(strFlagsFile, Bytes(strSpawnFlags.begin(), strSpawnFlags.end()))) {
      size_t ctLines = 0;
      for (char ch : strSpawnFlags) ctLines += ch == '\n';
      rep.Add("Spawn flags", std::to_string(ctLines - 6) + " entities exist only at some difficulties; their flags are in "
                             + FileBase(strFlagsFile) + ".spawnflags, for AlphaFixes' Difficulty setting");
    } else {
      rep.Add("Spawn flags", "cannot write " + strFlagsFile);
      iExit = 2;
    }
  }
  if (bEngineRan) {
    for (const std::string &str : engine.Messages()) rep.Add("Engine messages", str);
  }

  std::string strText = "WldConvert: " + strIn + "\n         -> " + strOut + "\n" + rep.Text();
  printf("%s", strText.c_str());
  if (!strReport.empty()) {
    std::ofstream f(strReport, std::ios::binary | std::ios::trunc);
    f << strText;
  }
  fflush(stdout);
  fflush(stderr);
  // the alpha's engine isn't shut down cleanly; end the process right away
  if (bEngineRan) TerminateProcess(GetCurrentProcess(), iExit);
  return iExit;
}
