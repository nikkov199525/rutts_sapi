@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ===========================================================================
rem  Build the ruTTS SAPI voice (rutts_sapi.dll) with an LLVM-MinGW toolchain,
rem  no Visual Studio required. The engine talks to SAPI through the portable
rem  src\RuTtsSapiDdk.h (a vendored subset of <sapiddk.h> on mingw), and DLL
rem  exports come from src\rutts_sapi.def instead of the MSVC-only #pragma.
rem
rem  The ru_tts core and the Rulex bridge are loaded at run time (LoadLibrary),
rem  so only libsamplerate is compiled in statically. The result is a
rem  self-contained COM in-proc server.
rem
rem  BOTH bitnesses are built by default: JAWS is a 32-bit host and needs the
rem  32-bit DLL, while 64-bit SAPI clients need the 64-bit one.
rem
rem  Usage: build_sapi_mingw.bat [Release|Debug] [x64|x86|both]   (default: Release both)
rem  Register the MATCHING bitness (admin):
rem     64-bit:  %WINDIR%\System32\regsvr32  out\mingw\x64\rutts_sapi.dll
rem     32-bit:  %WINDIR%\SysWOW64\regsvr32  out\mingw\x86\rutts_sapi.dll
rem ===========================================================================

cd /d "%~dp0"
set "ROOT=%CD%"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
set "WANT=%~2"
if "%WANT%"=="" set "WANT=both"

rem --- locate the toolchain bin directory ------------------------------------
set "BINDIR="
for /f "delims=" %%I in ('where x86_64-w64-mingw32-clang++ 2^>nul') do if not defined BINDIR for %%D in ("%%~dpI.") do set "BINDIR=%%~fD"
if not defined BINDIR (
  for /d %%I in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT_*\llvm-mingw-*") do (
    if exist "%%~I\bin\x86_64-w64-mingw32-clang++.exe" set "BINDIR=%%~I\bin"
  )
)
if not defined BINDIR (
  echo [ERROR] LLVM-MinGW not found. Install it ^(winget install MartinStorsjo.LLVM-MinGW.UCRT^)
  echo         or put x86_64-w64-mingw32-clang++ on PATH.
  exit /b 1
)
echo [INFO] toolchain: %BINDIR%

if /I "%WANT%"=="both" (
  call :build_arch x86_64 x64 || exit /b 1
  call :build_arch i686   x86 || exit /b 1
) else if /I "%WANT%"=="x64" (
  call :build_arch x86_64 x64 || exit /b 1
) else if /I "%WANT%"=="x86" (
  call :build_arch i686 x86 || exit /b 1
) else (
  echo [ERROR] unknown platform "%WANT%" ^(use x64, x86 or both^).
  exit /b 1
)
exit /b 0

rem ===========================================================================
rem  :build_arch <mingw-triple-arch> <out-subdir>
rem     %1 = x86_64 | i686        %2 = x64 | x86
rem ===========================================================================
:build_arch
set "TARCH=%~1"
set "TSUB=%~2"
set "CXX=%BINDIR%\%TARCH%-w64-mingw32-clang++.exe"
set "CC=%BINDIR%\%TARCH%-w64-mingw32-clang.exe"
if not exist "%CXX%" ( echo [ERROR] missing compiler: %CXX% & exit /b 1 )

set "OUTDIR=%ROOT%\out\mingw\%TSUB%"
set "OBJDIR=%ROOT%\build\mingw\%TSUB%\obj"

set "WARN=-Wall -Wno-unused-variable -Wno-unused-function -Wno-unknown-pragmas"
set "DEFS=-DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00"
set "OPT=-O2 -DNDEBUG"
if /I "%CONFIG%"=="Debug" set "OPT=-O0 -g"
set "INCLUDES=-I%ROOT%\src -I%ROOT%\libsamplerate\src"
set "CXXFLAGS=%OPT% %WARN% %DEFS% -std=c++17 -municode -ffunction-sections -fdata-sections"
rem HAVE_LRINT/HAVE_LRINTF: libsamplerate float_cast.h otherwise falls back to an
rem MSVC-only `_asm` block on 32-bit Windows, which clang can't parse. mingw has
rem the C99 lrint/lrintf, so route it there (config.h leaves both #undef).
set "CFLAGS=%OPT% %WARN% %DEFS% -DHAVE_LRINT=1 -DHAVE_LRINTF=1 -ffunction-sections -fdata-sections"
rem --enable-stdcall-fixup maps the .def's undecorated names onto the x86
rem _Name@N stdcall symbols, so COM finds DllGetClassObject etc. on 32-bit too.
set "LDFLAGS=-shared -static -static-libgcc -static-libstdc++ -Wl,--gc-sections -Wl,--enable-stdcall-fixup"
set "SYSLIBS=-lole32 -loleaut32 -lshlwapi -ladvapi32 -lshell32 -luuid"

rem RuTtsSapiGuids.cpp defines SPDFID_WaveFormatEx here (MSVC gets it from sapi.lib).
set "CPP_SRC=src\RuTtsSapiEngine.cpp src\DllMain.cpp src\ComReg.cpp src\RuTtsApi.cpp src\RulexClient.cpp src\ParamReader.cpp src\Koi8r.cpp src\WinUtil.cpp src\RuTtsSapiGuids.cpp"
set "C_SRC=libsamplerate\src\samplerate.c libsamplerate\src\src_linear.c libsamplerate\src\src_zoh.c"
set "DEF=src\rutts_sapi.def"
set "DLL=%OUTDIR%\rutts_sapi.dll"

if not exist "%OBJDIR%" mkdir "%OBJDIR%"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

set "OBJS="
echo [BUILD %TSUB%] compiling C++ ...
for %%F in (%CPP_SRC%) do (
  set "OBJ=%OBJDIR%\%%~nF.o"
  "%CXX%" %CXXFLAGS% %INCLUDES% -c "%ROOT%\%%F" -o "!OBJ!"
  if errorlevel 1 ( echo [ERROR] compile failed: %%F ^(%TSUB%^) & exit /b 1 )
  set "OBJS=!OBJS! "!OBJ!""
)
echo [BUILD %TSUB%] compiling C ^(libsamplerate^) ...
for %%F in (%C_SRC%) do (
  set "OBJ=%OBJDIR%\%%~nF.o"
  "%CC%" %CFLAGS% %INCLUDES% -c "%ROOT%\%%F" -o "!OBJ!"
  if errorlevel 1 ( echo [ERROR] compile failed: %%F ^(%TSUB%^) & exit /b 1 )
  set "OBJS=!OBJS! "!OBJ!""
)

echo [LINK  %TSUB%] %DLL%
"%CXX%" %LDFLAGS% -o "%DLL%" !OBJS! "%ROOT%\%DEF%" %SYSLIBS%
if errorlevel 1 ( echo [ERROR] link failed ^(%TSUB%^). & exit /b 1 )
echo [OK    %TSUB%] %DLL%
exit /b 0
