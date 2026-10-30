// Settings in AlphaFixes.ini (next to SeriousSam.exe) and the log file.
#include "alphafixes.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

Config g_cfg;

static std::string IniPath() { return GameDir() + "AlphaFixes.ini"; }

static int ReadInt(const char *strSection, const char *strKey, int iDefault)
{
  return (int)GetPrivateProfileIntA(strSection, strKey, iDefault, IniPath().c_str());
}

static float ReadFloat(const char *strSection, const char *strKey, float fDefault)
{
  char str[64];
  GetPrivateProfileStringA(strSection, strKey, "", str, sizeof(str), IniPath().c_str());
  if (str[0] == 0) return fDefault;
  return (float)atof(str);
}

static void WriteString(const char *strSection, const char *strKey, const char *strValue)
{
  WritePrivateProfileStringA(strSection, strKey, strValue, IniPath().c_str());
}

static void WriteInt(const char *strSection, const char *strKey, int iValue)
{
  char str[32];
  snprintf(str, sizeof(str), "%d", iValue);
  WriteString(strSection, strKey, str);
}

static void WriteFloat(const char *strSection, const char *strKey, float fValue)
{
  char str[32];
  snprintf(str, sizeof(str), "%.3f", fValue);
  WriteString(strSection, strKey, str);
}

static float Clamp(float f, float fMin, float fMax) { return f < fMin ? fMin : (f > fMax ? fMax : f); }

// Key names for the overlay hotkey: F1..F24 and a few others.
static const struct { int vk; const char *strName; } _aKeyNames[] = {
  { VK_INSERT, "Insert" }, { VK_HOME, "Home" }, { VK_END, "End" }, { VK_PRIOR, "PageUp" },
  { VK_NEXT, "PageDown" }, { VK_DELETE, "Delete" }, { VK_SCROLL, "ScrollLock" }, { VK_PAUSE, "Pause" },
};

const char *KeyName(int vk)
{
  static char str[16];
  if (vk >= VK_F1 && vk <= VK_F24) {
    snprintf(str, sizeof(str), "F%d", vk - VK_F1 + 1);
    return str;
  }
  for (const auto &k : _aKeyNames) {
    if (k.vk == vk) return k.strName;
  }
  snprintf(str, sizeof(str), "0x%02X", vk);
  return str;
}

static int ParseKey(const char *str, int iDefault)
{
  if ((str[0] == 'F' || str[0] == 'f') && str[1] >= '1' && str[1] <= '9') {
    int iF = atoi(str + 1);
    if (iF >= 1 && iF <= 24) return VK_F1 + iF - 1;
  }
  for (const auto &k : _aKeyNames) {
    if (_stricmp(k.strName, str) == 0) return k.vk;
  }
  if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
    int vk = (int)strtol(str + 2, nullptr, 16);
    if (vk > 0 && vk < 256) return vk;
  }
  return iDefault;
}

void LoadConfig()
{
  Config def;
  char str[64];

  // the mod was called SamOriginFixes before 0.5: keep the settings of an earlier install
  std::string strOldIni = GameDir() + "SamOriginFixes.ini";
  if (GetFileAttributesA(IniPath().c_str()) == INVALID_FILE_ATTRIBUTES
   && GetFileAttributesA(strOldIni.c_str()) != INVALID_FILE_ATTRIBUTES) {
    Log("Settings: %s", MoveFileA(strOldIni.c_str(), IniPath().c_str()) ? "SamOriginFixes.ini renamed to AlphaFixes.ini"
                                                                         : "could not rename SamOriginFixes.ini");
  }

  GetPrivateProfileStringA("Overlay", "ToggleKey", KeyName(def.iToggleKey), str, sizeof(str), IniPath().c_str());
  g_cfg.iToggleKey = ParseKey(str, def.iToggleKey);
  g_cfg.bShowFps = ReadInt("Overlay", "ShowFps", def.bShowFps) != 0;

  g_cfg.bInGameMenu = ReadInt("Menu", "InGameMenu", def.bInGameMenu) != 0;

  g_cfg.bDpiAware = ReadInt("Display", "DpiAware", def.bDpiAware) != 0;
  g_cfg.iRefreshRate = ReadInt("Display", "RefreshRate", def.iRefreshRate);
  g_cfg.eFullscreenMode = ReadInt("Display", "Borderless", 0) != 0 ? FullscreenMode::Borderless : FullscreenMode::Exclusive;
  g_cfg.bModernResolutions = ReadInt("Display", "ModernResolutions", def.bModernResolutions) != 0;

  g_cfg.bHorPlusFov = ReadInt("Widescreen", "HorPlusFov", def.bHorPlusFov) != 0;
  g_cfg.bFixHud = ReadInt("Widescreen", "FixHud", def.bFixHud) != 0;
  g_cfg.bMenu43 = ReadInt("Widescreen", "Menu43", def.bMenu43) != 0;
  g_cfg.fFov = Clamp(ReadFloat("Widescreen", "Fov", def.fFov), 60.0f, 130.0f);

  g_cfg.bRawInput = ReadInt("Mouse", "RawInput", def.bRawInput) != 0;
  g_cfg.fSensitivity = Clamp(ReadFloat("Mouse", "Sensitivity", def.fSensitivity), 0.01f, 20.0f);
  g_cfg.fVerticalRatio = Clamp(ReadFloat("Mouse", "VerticalRatio", def.fVerticalRatio), 0.1f, 4.0f);
  g_cfg.bLowLatencyLook = ReadInt("Mouse", "LowLatencyLook", def.bLowLatencyLook) != 0;

  g_cfg.bWheelWeapons = ReadInt("Weapons", "MouseWheel", def.bWheelWeapons) != 0;
  g_cfg.bWheelInvert = ReadInt("Weapons", "InvertWheel", def.bWheelInvert) != 0;
  g_cfg.bWeaponBar = ReadInt("Weapons", "WeaponBar", def.bWeaponBar) != 0;
  g_cfg.bFixedCrosshair = ReadInt("Weapons", "FixedCrosshair", def.bFixedCrosshair) != 0;

  g_cfg.iMasterVolume = ReadInt("Sound", "MasterVolume", def.iMasterVolume);
  if (g_cfg.iMasterVolume < 0 || g_cfg.iMasterVolume > 16) g_cfg.iMasterVolume = def.iMasterVolume;
  g_cfg.bSoftClipping = ReadInt("Sound", "SoftClipping", def.bSoftClipping) != 0;

  g_cfg.bVSync = ReadInt("Graphics", "VSync", def.bVSync) != 0;
  g_cfg.iAnisotropy = ReadInt("Graphics", "Anisotropy", def.iAnisotropy);
  g_cfg.iAnisotropy = g_cfg.iAnisotropy >= 16 ? 16 : g_cfg.iAnisotropy >= 8 ? 8 : g_cfg.iAnisotropy >= 4 ? 4 : g_cfg.iAnisotropy >= 2 ? 2 : 1;
  g_cfg.iAntiAliasing = ReadInt("Graphics", "AntiAliasing", def.iAntiAliasing);
  g_cfg.iAntiAliasing = g_cfg.iAntiAliasing >= 8 ? 8 : g_cfg.iAntiAliasing >= 4 ? 4 : g_cfg.iAntiAliasing >= 2 ? 2 : 0;

  g_cfg.bStreamFix = ReadInt("Fixes", "StreamFix", def.bStreamFix) != 0;
  g_cfg.bLightFix = ReadInt("Fixes", "LightFix", def.bLightFix) != 0;
  g_cfg.bHideNotPrecached = ReadInt("Fixes", "HideNotPrecached", def.bHideNotPrecached) != 0;
  g_cfg.bOwnSettingsFile = ReadInt("Fixes", "OwnSettingsFile", def.bOwnSettingsFile) != 0;

  g_cfg.bUnlockCannon = ReadInt("Gameplay", "UnlockCannon", def.bUnlockCannon) != 0;
  g_cfg.bTfeThirdPerson = ReadInt("Gameplay", "TfeThirdPerson", def.bTfeThirdPerson) != 0;
  g_cfg.bMenuPause = ReadInt("Gameplay", "MenuPause", def.bMenuPause) != 0;

  // write the file back so every setting is listed in it
  SaveConfig();
}

void SaveConfig()
{
  WriteString("Overlay", "ToggleKey", KeyName(g_cfg.iToggleKey));
  WriteInt("Overlay", "ShowFps", g_cfg.bShowFps);

  WriteInt("Menu", "InGameMenu", g_cfg.bInGameMenu);

  WriteInt("Display", "DpiAware", g_cfg.bDpiAware);
  WriteInt("Display", "RefreshRate", g_cfg.iRefreshRate);
  WriteInt("Display", "Borderless", g_cfg.eFullscreenMode == FullscreenMode::Borderless);
  WriteInt("Display", "ModernResolutions", g_cfg.bModernResolutions);

  WriteInt("Widescreen", "HorPlusFov", g_cfg.bHorPlusFov);
  WriteInt("Widescreen", "FixHud", g_cfg.bFixHud);
  WriteInt("Widescreen", "Menu43", g_cfg.bMenu43);
  WriteFloat("Widescreen", "Fov", g_cfg.fFov);

  WriteInt("Mouse", "RawInput", g_cfg.bRawInput);
  WriteFloat("Mouse", "Sensitivity", g_cfg.fSensitivity);
  WriteFloat("Mouse", "VerticalRatio", g_cfg.fVerticalRatio);
  WriteInt("Mouse", "LowLatencyLook", g_cfg.bLowLatencyLook);

  WriteInt("Weapons", "MouseWheel", g_cfg.bWheelWeapons);
  WriteInt("Weapons", "InvertWheel", g_cfg.bWheelInvert);
  WriteInt("Weapons", "WeaponBar", g_cfg.bWeaponBar);
  WriteInt("Weapons", "FixedCrosshair", g_cfg.bFixedCrosshair);

  WriteInt("Sound", "MasterVolume", g_cfg.iMasterVolume);
  WriteInt("Sound", "SoftClipping", g_cfg.bSoftClipping);

  WriteInt("Graphics", "VSync", g_cfg.bVSync);
  WriteInt("Graphics", "Anisotropy", g_cfg.iAnisotropy);
  WriteInt("Graphics", "AntiAliasing", g_cfg.iAntiAliasing);

  WriteInt("Fixes", "StreamFix", g_cfg.bStreamFix);
  WriteInt("Fixes", "LightFix", g_cfg.bLightFix);
  WriteInt("Fixes", "HideNotPrecached", g_cfg.bHideNotPrecached);
  WriteInt("Fixes", "OwnSettingsFile", g_cfg.bOwnSettingsFile);

  WriteInt("Gameplay", "UnlockCannon", g_cfg.bUnlockCannon);
  WriteInt("Gameplay", "TfeThirdPerson", g_cfg.bTfeThirdPerson);
  WriteInt("Gameplay", "MenuPause", g_cfg.bMenuPause);
}

// ---------------------------------------------------------------------------

static FILE *_pfLog = nullptr;
static CRITICAL_SECTION _csLog;

void LogOpen()
{
  InitializeCriticalSection(&_csLog);
  _pfLog = fopen((GameDir() + "AlphaFixes.log").c_str(), "w");
}

void Log(const char *strFormat, ...)
{
  if (_pfLog == nullptr) return;
  EnterCriticalSection(&_csLog);
  va_list args;
  va_start(args, strFormat);
  vfprintf(_pfLog, strFormat, args);
  va_end(args);
  fputc('\n', _pfLog);
  fflush(_pfLog);
  LeaveCriticalSection(&_csLog);
}
