@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ===========================================================================
rem  Build the ruTTS configurator (rutts_configurator.exe) with an LLVM-MinGW
rem  toolchain, no Visual Studio required. GUI app: reads/writes the ru_tts INI
rem  and the [Characters]/[SingleCharacters] dictionaries.
rem
rem  Usage: build_configurator_mingw.bat [Release|Debug]   (default Release)
rem ===========================================================================

cd /d "%~dp0"
set "ROOT=%CD%"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "OUTDIR=%ROOT%\out\mingw"
set "OBJDIR=%ROOT%\build\mingw\obj"

rem --- locate the toolchain --------------------------------------------------
set "CXX="
for /f "delims=" %%I in ('where x86_64-w64-mingw32-clang++ 2^>nul') do if not defined CXX set "CXX=%%I"
if not defined CXX (
  for /d %%I in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT_*\llvm-mingw-*") do (
    if exist "%%~I\bin\x86_64-w64-mingw32-clang++.exe" set "CXX=%%~I\bin\x86_64-w64-mingw32-clang++.exe"
  )
)
if not defined CXX (
  echo [ERROR] x86_64-w64-mingw32-clang++ not found. Install LLVM-MinGW
  echo         ^(winget install MartinStorsjo.LLVM-MinGW.UCRT^) or put it on PATH.
  exit /b 1
)
echo [INFO] CXX = %CXX%

rem --- flags -----------------------------------------------------------------
set "WARN=-Wall -Wno-unused-variable -Wno-unused-function -Wno-unknown-pragmas"
set "DEFS=-DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00"
set "OPT=-O2 -DNDEBUG"
if /I "%CONFIG%"=="Debug" set "OPT=-O0 -g"
set "INCLUDES=-I%ROOT%\src"
set "CXXFLAGS=%OPT% %WARN% %DEFS% -std=c++17 -municode -ffunction-sections -fdata-sections"
rem -mwindows: GUI subsystem (no console window). -municode: wWinMain entry.
set "LDFLAGS=-mwindows -municode -static -static-libgcc -static-libstdc++ -Wl,--gc-sections"
set "SYSLIBS=-lcomctl32 -lshell32 -lole32 -ladvapi32 -luuid"

set "CPP_SRC=src\IniConfigApp.cpp src\ParamReader.cpp"
set "EXE=%OUTDIR%\rutts_configurator.exe"

if not exist "%OBJDIR%" mkdir "%OBJDIR%"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

set "OBJS="
echo [BUILD] compiling ...
for %%F in (%CPP_SRC%) do (
  set "OBJ=%OBJDIR%\cfg_%%~nF.o"
  "%CXX%" %CXXFLAGS% %INCLUDES% -c "%ROOT%\%%F" -o "!OBJ!"
  if errorlevel 1 ( echo [ERROR] compile failed: %%F & exit /b 1 )
  set "OBJS=!OBJS! "!OBJ!""
)

echo [LINK]  %EXE%
"%CXX%" %LDFLAGS% -o "%EXE%" !OBJS! %SYSLIBS%
if errorlevel 1 ( echo [ERROR] link failed. & exit /b 1 )

echo [OK] configurator built: %EXE%
exit /b 0
