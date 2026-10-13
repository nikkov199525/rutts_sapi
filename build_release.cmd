@echo off
rem Keep the traditional entry point; CMake owns the complete release workflow.
setlocal EnableExtensions
set "CMAKE_EXE=cmake"
where /q cmake && goto :run
rem No CMake on PATH: take the one that ships with Visual Studio.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do set "CMAKE_EXE=%%I"
if "%CMAKE_EXE%"=="cmake" (
  echo [ERROR] CMake not found. Install CMake or the CMake tools of Visual Studio.
  exit /b 1
)
:run
"%CMAKE_EXE%" -P "%~dp0cmake\Release.cmake"
exit /b %errorlevel%
