@echo off
rem Builds build\pdd.exe (the window) and build\pdd-headless.exe (for tests
rem and scripted runs) with MSVC.  If cl.exe is not on PATH, vcvars64.bat
rem is looked for: the VS2019 Build Tools first, then whatever vswhere finds.
setlocal
cd /d "%~dp0"

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
  echo No MSVC compiler found. Install the Visual Studio 2019 Build Tools with the C++ toolchain.
  exit /b 1
)

if not exist build\obj\headless mkdir build\obj\headless
set CFLAGS=/nologo /W4 /O2 /D_CRT_SECURE_NO_WARNINGS
set ENGINE=src\pd_main.c src\pd1.c src\pd2.c src\pd_video.c src\pd_sprite.c src\pd_text.c src\pd_keys.c src\pd_files.c src\pd_sound.c src\pd_idle.c src\pd_ball.c src\pd_lights.c src\pd_game.c src\pd_events.c src\pd_play.c src\pd_rules.c src\pd_bcd.c src\pd_lost.c src\pd_handlers.c src\pd_over.c src\code.c src\mem.c src\menu.c
set CORE=src\main.c src\pad.c src\fli.c src\frame.c src\sound.c src\audiofx.c src\hud.c src\launcher.c src\gog.c src\textmode.c src\modplay.c src\vga.c src\sys.c src\sha256.c

cl %CFLAGS% /Fobuild\obj\ /Fe:build\pdd.exe %ENGINE% %CORE% src\plat_win32.c user32.lib gdi32.lib winmm.lib advapi32.lib shell32.lib /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fobuild\obj\headless\ /Fe:build\pdd-headless.exe %ENGINE% %CORE% src\plat_null.c advapi32.lib shell32.lib
if errorlevel 1 exit /b 1
