@echo off
rem Builds build\pddrun.exe (the headless runner, see main.c) with MSVC.
rem If cl.exe is not on PATH, vcvars64.bat is looked for as pfemu's and
rem pfnative's build.bat do: the VS2019 Build Tools first, then vswhere.
setlocal
cd /d "%~dp0\..\.."

where cl >nul 2>&1
if %errorlevel% neq 0 (
  if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
  ) else (
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
      if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
    )
  )
)
where cl >nul 2>&1
if %errorlevel% neq 0 (
  echo No MSVC compiler found. Install Visual Studio 2019 Build Tools with the C++ toolchain.
  exit /b 1
)

if not exist build\obj-run mkdir build\obj-run
rem /fp:precise and no /fp:fast: the timing is doubles, compared exactly
cl /nologo /O2 /W3 /fp:precise /D_CRT_SECURE_NO_WARNINGS /Fo:build\obj-run\ /Fe:build\pddrun.exe ^
  tools\run\main.c tools\run\cpu.c tools\run\vga.c tools\run\dev.c tools\run\bios.c ^
  tools\run\dos.c tools\run\sound.c tools\run\vgafont.c tools\run\png.c
if errorlevel 1 exit /b 1
