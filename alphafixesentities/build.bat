@echo off
rem Builds AlphaFixesEntities.dll (32-bit).
rem   build.bat            release build into build\Release
rem   build.bat install    release build, then copy the DLL into ..\Bin and its data into the game
setlocal
cd /d "%~dp0"

set "INSTALL="
for %%a in (%*) do (
  if /i "%%a"=="install" set "INSTALL=1"
)

set "CMAKE=cmake"
where cmake >nul 2>nul
if not errorlevel 1 goto :configure
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :nocmake
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" goto :nocmake

:configure
"%CMAKE%" -S . -B build -A Win32 || exit /b 1
"%CMAKE%" --build build --config Release || exit /b 1
if defined INSTALL "%CMAKE%" --install build --config Release || exit /b 1
echo.
echo Built build\Release\AlphaFixesEntities.dll
exit /b 0

:nocmake
echo CMake not found. Install Visual Studio with the "Desktop development with C++" workload.
exit /b 1
