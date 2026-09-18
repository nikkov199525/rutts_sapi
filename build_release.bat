@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "ROOT=%CD%"
set "SOLUTION=%ROOT%\rutts_sapi.sln"
set "ISS_FILE=%ROOT%\out\rutts_sapi.iss"
set "PKG_ROOT=%ROOT%\out\ru_tts"
set "DOC_SRC=%ROOT%\src\doc"
set "DOC_DST=%PKG_ROOT%\doc"

call :find_msbuild || exit /b 1

call :build_platform Win32 || exit /b 1
call :build_platform x64 || exit /b 1
if not exist "%PKG_ROOT%" mkdir "%PKG_ROOT%"
if not exist "%PKG_ROOT%\lib\x32" mkdir "%PKG_ROOT%\lib\x32"
if not exist "%PKG_ROOT%\lib\x64" mkdir "%PKG_ROOT%\lib\x64"
if not exist "%DOC_DST%" mkdir "%DOC_DST%"

copy /Y "%ROOT%\out\Win32\Release\rutts_sapi.dll" "%PKG_ROOT%\lib\x32\" >nul || goto :copy_fail
copy /Y "%ROOT%\out\Win32\Release\rutts_rulex.dll" "%PKG_ROOT%\lib\x32\" >nul || goto :copy_fail
copy /Y "%ROOT%\out\x64\Release\rutts_sapi.dll" "%PKG_ROOT%\lib\x64\" >nul || goto :copy_fail
copy /Y "%ROOT%\out\x64\Release\rutts_rulex.dll" "%PKG_ROOT%\lib\x64\" >nul || goto :copy_fail
copy /Y "%ROOT%\out\x64\Release\rutts_configurator.exe" "%PKG_ROOT%\" >nul || goto :copy_fail

if exist "%DOC_SRC%\readme.html" (
  copy /Y "%DOC_SRC%\readme.html" "%DOC_DST%\readme.html" >nul || goto :copy_fail
)

if exist "%DOC_SRC%\LICENSES" (
  xcopy "%DOC_SRC%\LICENSES" "%DOC_DST%\LICENSES\" /E /I /Y >nul || goto :copy_fail
)

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
call :require_file "%ISS_FILE%" || exit /b 1

call :find_iscc || exit /b 1

echo [BUILD] Inno Setup package...
"%ISCC%" "%ISS_FILE%"
if errorlevel 1 (
  echo [ERROR] ISCC failed.
  exit /b 1
)
exit /b 0

:build_platform
set "PLATFORM=%~1"
"%MSBUILD%" "%SOLUTION%" /m /nologo /p:Configuration=Release /p:Platform=%PLATFORM%
if errorlevel 1 (
  echo [ERROR] Build failed for %PLATFORM%.
  exit /b 1
)
exit /b 0

:require_file
if exist "%~1" exit /b 0
echo [ERROR] Missing required file: %~1
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
    echo [INFO] ISCC: %ISCC%
    exit /b 0
  )
)

echo [ERROR] ISCC.exe not found. Install Inno Setup 6 or add ISCC to PATH.
exit /b 1

:copy_fail
echo [ERROR] Copy step failed.
exit /b 1
