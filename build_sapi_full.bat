@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

set "ROOT=%CD%"
set "SOLUTION=%ROOT%\rutts_sapi.sln"
set "PKG_ROOT=%ROOT%\out\ru_tts"
set "DOC_SRC=%ROOT%\src\doc"
set "DOC_DST=%PKG_ROOT%\doc"
set "ISS_FILE=%ROOT%\out\rutts_sapi.iss"
set "CLEAN_EXTERNAL="
set "BUILD_INSTALLER="
set "BUILD_NVDA="

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="clean" set "CLEAN_EXTERNAL=1"
if /I "%~1"=="installer" set "BUILD_INSTALLER=1"
if /I "%~1"=="nvda" set "BUILD_NVDA=1"
shift
goto :parse_args

:args_done
call :find_llvm_mingw || exit /b 1
call :find_git_usr || exit /b 1
call :find_cmake || exit /b 1
call :find_msbuild || exit /b 1
if defined BUILD_NVDA call :find_nvda_tools || exit /b 1

call :ensure_junction "%SystemDrive%\rutts-src" "%ROOT%" ASCII_ROOT || exit /b 1
call :ensure_junction "%SystemDrive%\llvm-mingw" "%LLVM_MINGW_ROOT%" LLVM_MINGW_ASCII || exit /b 1
call :ensure_junction "%SystemDrive%\vs-cmake" "%CMAKE_ROOT%" CMAKE_ASCII || exit /b 1
call :ensure_build_tools || exit /b 1

set "PATH=%BUILD_TOOLS%;%LLVM_MINGW_ASCII%\bin;%GIT_USR%;%CMAKE_ASCII%\bin;%PATH%"
set "EXT_SRC=%ASCII_ROOT%\_external\ru_tts-for-nvda\src"
set "EXT_SYNTH=%EXT_SRC%\synthDrivers\ru_tts"

call :repair_pcre2_deps "%EXT_SRC%\.deps" || exit /b 1
call :repair_pcre2_deps "%EXT_SRC%\.deps\i686-w64-mingw32" || exit /b 1
call :repair_pcre2_deps "%EXT_SRC%\.deps\x86_64-w64-mingw32" || exit /b 1

if not exist "%EXT_SRC%\Makefile" (
  echo [ERROR] Missing external source Makefile: %EXT_SRC%\Makefile
  exit /b 1
)

if defined CLEAN_EXTERNAL (
  echo [BUILD] Cleaning external ru_tts/rulex outputs...
  pushd "%EXT_SRC%" || exit /b 1
  "%LLVM_MINGW_ASCII%\bin\mingw32-make.exe" clean-all
  if errorlevel 1 (
    popd
    echo [ERROR] External clean failed.
    exit /b 1
  )
  popd
)

echo [BUILD] Fresh ru_tts.dll, rulex.dll and rulex.db from sources...
pushd "%EXT_SRC%" || exit /b 1
"%LLVM_MINGW_ASCII%\bin\mingw32-make.exe" WITH_X32=yes WITH_X64=yes WITH_RULEX=yes ^
  synthDrivers/ru_tts/lib/x32/ru_tts.dll ^
  synthDrivers/ru_tts/lib/x32/rulex.dll ^
  synthDrivers/ru_tts/lib/x64/ru_tts.dll ^
  synthDrivers/ru_tts/lib/x64/rulex.dll ^
  synthDrivers/ru_tts/rulex.db
if errorlevel 1 (
  popd
  echo [ERROR] External runtime build failed.
  exit /b 1
)

echo [BUILD] Refreshing rulex.dll after rulex source changes...
del /Q "%EXT_SYNTH%\lib\x32\rulex.dll" 2>nul
del /Q "%EXT_SYNTH%\lib\x64\rulex.dll" 2>nul
"%LLVM_MINGW_ASCII%\bin\mingw32-make.exe" WITH_X32=yes WITH_X64=yes WITH_RULEX=yes ^
  synthDrivers/ru_tts/lib/x32/rulex.dll ^
  synthDrivers/ru_tts/lib/x64/rulex.dll
if errorlevel 1 (
  popd
  echo [ERROR] rulex refresh failed.
  exit /b 1
)
popd

call :prepare_package_dirs || exit /b 1
call :copy_external_runtime || exit /b 1

call :build_platform Win32 || exit /b 1
call :build_platform x64 || exit /b 1

call :copy_sapi_outputs || exit /b 1
call :copy_docs || exit /b 1
call :verify_outputs || exit /b 1

if defined BUILD_NVDA (
  echo [BUILD] NVDA add-on...
  pushd "%EXT_SRC%" || exit /b 1
  "%LLVM_MINGW_ASCII%\bin\mingw32-make.exe" WITH_X32=yes WITH_X64=yes WITH_RULEX=yes ru_tts.nvda-addon
  if errorlevel 1 (
    popd
    echo [ERROR] NVDA add-on build failed.
    exit /b 1
  )
  popd
  if not exist "%ROOT%\out\NVDA" mkdir "%ROOT%\out\NVDA"
  copy /Y "%EXT_SRC%\ru_tts.nvda-addon" "%ROOT%\out\NVDA\ru_tts.nvda-addon" >nul || goto :copy_fail
  call :require_file "%ROOT%\out\NVDA\ru_tts.nvda-addon" || exit /b 1
  echo [OK] NVDA add-on: %ROOT%\out\NVDA\ru_tts.nvda-addon
)

if defined BUILD_INSTALLER (
  call :find_iscc || exit /b 1
  echo [BUILD] Inno Setup package...
  "!ISCC!" "%ISS_FILE%"
  if errorlevel 1 (
    echo [ERROR] ISCC failed.
    exit /b 1
  )
)

echo [OK] Full SAPI build is ready: %PKG_ROOT%
exit /b 0

:prepare_package_dirs
if not exist "%PKG_ROOT%" mkdir "%PKG_ROOT%"
if not exist "%PKG_ROOT%\lib\x32" mkdir "%PKG_ROOT%\lib\x32"
if not exist "%PKG_ROOT%\lib\x64" mkdir "%PKG_ROOT%\lib\x64"
if not exist "%DOC_DST%" mkdir "%DOC_DST%"
exit /b 0

:copy_external_runtime
copy /Y "%EXT_SYNTH%\lib\x32\ru_tts.dll" "%PKG_ROOT%\lib\x32\" >nul || goto :copy_fail
copy /Y "%EXT_SYNTH%\lib\x32\rulex.dll" "%PKG_ROOT%\lib\x32\" >nul || goto :copy_fail
copy /Y "%EXT_SYNTH%\lib\x64\ru_tts.dll" "%PKG_ROOT%\lib\x64\" >nul || goto :copy_fail
copy /Y "%EXT_SYNTH%\lib\x64\rulex.dll" "%PKG_ROOT%\lib\x64\" >nul || goto :copy_fail
copy /Y "%EXT_SYNTH%\rulex.db" "%PKG_ROOT%\rulex.db" >nul || goto :copy_fail
exit /b 0

:copy_sapi_outputs
copy /Y "%ROOT%\out\Win32\Release\rutts_sapi.dll" "%PKG_ROOT%\lib\x32\" >nul || goto :copy_fail
copy /Y "%ROOT%\out\Win32\Release\rutts_rulex.dll" "%PKG_ROOT%\lib\x32\" >nul || goto :copy_fail
copy /Y "%ROOT%\out\x64\Release\rutts_sapi.dll" "%PKG_ROOT%\lib\x64\" >nul || goto :copy_fail
copy /Y "%ROOT%\out\x64\Release\rutts_rulex.dll" "%PKG_ROOT%\lib\x64\" >nul || goto :copy_fail
copy /Y "%ROOT%\out\x64\Release\rutts_configurator.exe" "%PKG_ROOT%\" >nul || goto :copy_fail
exit /b 0

:copy_docs
if exist "%DOC_SRC%\readme.html" (
  copy /Y "%DOC_SRC%\readme.html" "%DOC_DST%\readme.html" >nul || goto :copy_fail
)

if exist "%DOC_SRC%\LICENSES" (
  xcopy "%DOC_SRC%\LICENSES" "%DOC_DST%\LICENSES\" /E /I /Y >nul || goto :copy_fail
)

if exist "%EXT_SYNTH%\LICENSES" (
  xcopy "%EXT_SYNTH%\LICENSES" "%DOC_DST%\LICENSES\" /E /I /Y >nul || goto :copy_fail
)

if exist "%EXT_SYNTH%\COPYING.txt" (
  copy /Y "%EXT_SYNTH%\COPYING.txt" "%DOC_DST%\COPYING.txt" >nul || goto :copy_fail
)
exit /b 0

:verify_outputs
call :require_file "%PKG_ROOT%\lib\x32\ru_tts.dll" || exit /b 1
call :require_file "%PKG_ROOT%\lib\x32\rulex.dll" || exit /b 1
call :require_file "%PKG_ROOT%\lib\x32\rutts_sapi.dll" || exit /b 1
call :require_file "%PKG_ROOT%\lib\x32\rutts_rulex.dll" || exit /b 1
call :require_file "%PKG_ROOT%\lib\x64\ru_tts.dll" || exit /b 1
call :require_file "%PKG_ROOT%\lib\x64\rulex.dll" || exit /b 1
call :require_file "%PKG_ROOT%\lib\x64\rutts_sapi.dll" || exit /b 1
call :require_file "%PKG_ROOT%\lib\x64\rutts_rulex.dll" || exit /b 1
call :require_file "%PKG_ROOT%\rutts_configurator.exe" || exit /b 1
call :require_file "%PKG_ROOT%\rulex.db" || exit /b 1
call :require_file "%DOC_DST%\readme.html" || exit /b 1
exit /b 0

:build_platform
set "PLATFORM=%~1"
echo [BUILD] SAPI solution %PLATFORM% Release...
"%MSBUILD%" "%SOLUTION%" /m /nologo /t:Rebuild /p:Configuration=Release /p:Platform=%PLATFORM%
if errorlevel 1 (
  echo [ERROR] MSBuild failed for %PLATFORM%.
  exit /b 1
)
exit /b 0

:find_llvm_mingw
if defined LLVM_MINGW_ROOT (
  if exist "%LLVM_MINGW_ROOT%\bin\x86_64-w64-mingw32-clang.exe" (
    echo [INFO] LLVM-MinGW: !LLVM_MINGW_ROOT!
    exit /b 0
  )
)

if defined LLVM_MINGW_BIN (
  if exist "%LLVM_MINGW_BIN%\x86_64-w64-mingw32-clang.exe" (
    for %%I in ("%LLVM_MINGW_BIN%\..") do set "LLVM_MINGW_ROOT=%%~fI"
    echo [INFO] LLVM-MinGW: !LLVM_MINGW_ROOT!
    exit /b 0
  )
)

if exist "%SystemDrive%\llvm-mingw\bin\x86_64-w64-mingw32-clang.exe" (
  set "LLVM_MINGW_ROOT=%SystemDrive%\llvm-mingw"
  echo [INFO] LLVM-MinGW: !LLVM_MINGW_ROOT!
  exit /b 0
)

for /f "usebackq delims=" %%I in (`where x86_64-w64-mingw32-clang.exe 2^>nul`) do (
  set "CLANG64=%%I"
  goto :llvm_found
)

echo [ERROR] LLVM-MinGW not found. Set LLVM_MINGW_ROOT or add x86_64-w64-mingw32-clang.exe to PATH.
exit /b 1

:llvm_found
for %%I in ("%CLANG64%") do set "LLVM_MINGW_BIN=%%~dpI"
for %%I in ("%LLVM_MINGW_BIN%\..") do set "LLVM_MINGW_ROOT=%%~fI"
echo [INFO] LLVM-MinGW: !LLVM_MINGW_ROOT!
exit /b 0

:find_git_usr
if exist "%ProgramFiles%\Git\usr\bin\rm.exe" (
  set "GIT_USR=%ProgramFiles%\Git\usr\bin"
  echo [INFO] Git usr tools: !GIT_USR!
  exit /b 0
)
if exist "%ProgramFiles(x86)%\Git\usr\bin\rm.exe" (
  set "GIT_USR=%ProgramFiles(x86)%\Git\usr\bin"
  echo [INFO] Git usr tools: !GIT_USR!
  exit /b 0
)
echo [ERROR] Git for Windows usr tools not found. Install Git or add rm/touch/mkdir to PATH.
exit /b 1

:find_cmake
if defined CMAKE_ROOT (
  if exist "%CMAKE_ROOT%\bin\cmake.exe" (
    echo [INFO] CMake: !CMAKE_ROOT!
    exit /b 0
  )
)

if exist "%SystemDrive%\vs-cmake\bin\cmake.exe" (
  set "CMAKE_ROOT=%SystemDrive%\vs-cmake"
  echo [INFO] CMake: !CMAKE_ROOT!
  exit /b 0
)

for %%I in (
  "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake"
  "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake"
) do (
  if exist "%%~I\bin\cmake.exe" (
    set "CMAKE_ROOT=%%~I"
    echo [INFO] CMake: !CMAKE_ROOT!
    exit /b 0
  )
)

for /f "usebackq delims=" %%I in (`where cmake.exe 2^>nul`) do (
  for %%J in ("%%~dpI\..") do set "CMAKE_ROOT=%%~fJ"
  echo [INFO] CMake: !CMAKE_ROOT!
  exit /b 0
)

echo [ERROR] CMake not found. Install Visual Studio CMake tools or set CMAKE_ROOT.
exit /b 1

:find_msbuild
set "MSBUILD=msbuild.exe"
where /q msbuild.exe
if %ERRORLEVEL% EQU 0 (
  echo [INFO] MSBuild: msbuild.exe
  exit /b 0
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD=%%I"
    goto :msbuild_found
  )
)

for %%I in (
  "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
  "%ProgramFiles%\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
) do (
  if exist "%%~I" (
    set "MSBUILD=%%~I"
    goto :msbuild_found
  )
)

echo [ERROR] MSBuild not found.
exit /b 1

:msbuild_found
echo [INFO] MSBuild: %MSBUILD%
exit /b 0

:find_iscc
set "ISCC=iscc.exe"
where /q iscc.exe
if %ERRORLEVEL% EQU 0 (
  echo [INFO] ISCC: iscc.exe
  exit /b 0
)

for %%I in (
  "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
  "%ProgramFiles%\Inno Setup 6\ISCC.exe"
) do (
  if exist "%%~I" (
    set "ISCC=%%~I"
    echo [INFO] ISCC: !ISCC!
    exit /b 0
  )
)

echo [ERROR] ISCC.exe not found. Install Inno Setup 6 or run build_sapi_full.bat without installer.
exit /b 1

:find_nvda_tools
where /q python.exe
if errorlevel 1 (
  echo [ERROR] Python not found. Install Python 3 or add python.exe to PATH.
  exit /b 1
)

where /q 7z.exe
if not errorlevel 1 exit /b 0

for %%I in (
  "%ProgramFiles%\7-Zip\7z.exe"
  "%ProgramFiles(x86)%\7-Zip\7z.exe"
) do (
  if exist "%%~I" (
    set "PATH=%%~dpI;!PATH!"
    exit /b 0
  )
)

echo [ERROR] 7z.exe not found. Install 7-Zip or add it to PATH.
exit /b 1

:ensure_junction
set "LINK_PATH=%~1"
set "LINK_TARGET=%~2"
set "%~3=%LINK_PATH%"
if exist "%LINK_PATH%\" exit /b 0
mklink /J "%LINK_PATH%" "%LINK_TARGET%" >nul
if errorlevel 1 (
  echo [WARN] Could not create junction %LINK_PATH%; using %LINK_TARGET%.
  set "%~3=%LINK_TARGET%"
)
exit /b 0

:ensure_build_tools
set "BUILD_TOOLS=%SystemDrive%\rutts-build-tools"
if not exist "%BUILD_TOOLS%" mkdir "%BUILD_TOOLS%"
copy /Y "%LLVM_MINGW_ASCII%\bin\mingw32-make.exe" "%BUILD_TOOLS%\make.exe" >nul || goto :copy_fail
exit /b 0

:repair_pcre2_deps
set "PCRE2_DEPS=%~1"
if not exist "%PCRE2_DEPS%\done" exit /b 0
if exist "%PCRE2_DEPS%\lib\libpcre2-8.a" (
  if exist "%PCRE2_DEPS%\lib\libpcre2-posix.a" exit /b 0
)
echo [WARN] Incomplete PCRE2 dependencies: %PCRE2_DEPS%
echo [WARN] Removing stale marker so Makefile can rebuild them.
del /F /Q "%PCRE2_DEPS%\done" >nul 2>nul
if exist "%PCRE2_DEPS%\done" (
  echo [ERROR] Could not remove stale marker: %PCRE2_DEPS%\done
  exit /b 1
)
exit /b 0

:require_file
if exist "%~1" exit /b 0
echo [ERROR] Missing required file: %~1
exit /b 1

:copy_fail
echo [ERROR] Copy step failed.
exit /b 1
