@echo off
rem Builds SamAlphaEditorFixes.dll, PatchWorldEditor.exe and obj2lwo.exe (32-bit) with Visual C++.
rem Run from a "x86 Native Tools Command Prompt", or let this script find vcvars32.bat.
setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if not errorlevel 1 goto :build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :nocl
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto :nocl
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
where cl >nul 2>nul
if errorlevel 1 goto :nocl

:build
if not exist build mkdir build
cl /nologo /O2 /W4 /MT /LD /D_CRT_SECURE_NO_WARNINGS SamAlphaEditorFixes.c ObjImport.c /Fobuild\ /Febuild\SamAlphaEditorFixes.dll /link /NOLOGO || exit /b 1
cl /nologo /O2 /W4 /MT /D_CRT_SECURE_NO_WARNINGS PatchWorldEditor.c /Fobuild\ /Febuild\PatchWorldEditor.exe /link /NOLOGO /MANIFEST:EMBED /MANIFESTUAC:"level='asInvoker' uiAccess='false'" || exit /b 1
cl /nologo /O2 /W4 /MT /D_CRT_SECURE_NO_WARNINGS obj2lwo.c ObjImport.c /Fobuild\ /Febuild\obj2lwo.exe /link /NOLOGO || exit /b 1
del build\*.obj build\*.exp build\*.lib 2>nul
echo.
echo Built build\SamAlphaEditorFixes.dll, build\PatchWorldEditor.exe and build\obj2lwo.exe
exit /b 0

:nocl
echo Visual C++ (cl.exe) not found. Install Visual Studio with the C++ workload.
exit /b 1
