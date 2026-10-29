@echo off
rem Builds winmm.dll and AlphaFixes.dll (32-bit).
rem   build.bat                release build into build\Release
rem   build.bat install        release build, then copy both DLLs into ..\Bin
rem   build.bat test           test build (with the testing aids) into build-test\Release
rem   build.bat test install   test build, then copy both DLLs into ..\Bin
setlocal
cd /d "%~dp0"

set "DIR=build"
set "AIDS=OFF"
set "INSTALL="
for %%a in (%*) do (
  if /i "%%a"=="test" set "DIR=build-test"& set "AIDS=ON"
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
"%CMAKE%" -S . -B %DIR% -A Win32 -DAF_TEST_AIDS=%AIDS% || exit /b 1
"%CMAKE%" --build %DIR% --config Release || exit /b 1
if defined INSTALL "%CMAKE%" --install %DIR% --config Release || exit /b 1
echo.
if "%AIDS%"=="ON" (echo Built %DIR%\Release\winmm.dll and %DIR%\Release\AlphaFixes.dll ^(test build, with testing aids^)) else (echo Built %DIR%\Release\winmm.dll and %DIR%\Release\AlphaFixes.dll ^(release^))
exit /b 0

:nocmake
echo CMake not found. Install Visual Studio with the "Desktop development with C++" workload.
exit /b 1
