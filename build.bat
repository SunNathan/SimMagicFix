@echo off
setlocal
set "CMAKE="

if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do set "CMAKE=%%i"
)

if not defined CMAKE if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE where cmake >nul 2>&1 && set "CMAKE=cmake"

if not defined CMAKE (
  echo CMake not found. Install Visual Studio 2022 with the C++ workload or CMake.
  exit /b 1
)

"%CMAKE%" --preset vs2022-x64 %*
if errorlevel 1 exit /b 1
"%CMAKE%" --build --preset release
if errorlevel 1 exit /b 1
