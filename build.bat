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

rem Locate vcpkg for manifest mode.
if not defined VCPKG_ROOT if defined VCPKG_INSTALLATION_ROOT set "VCPKG_ROOT=%VCPKG_INSTALLATION_ROOT%"
if not defined VCPKG_ROOT if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_ROOT=C:\vcpkg"
if not defined VCPKG_ROOT (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath 2^>nul`) do (
    if exist "%%i\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_ROOT=%%i\VC\vcpkg"
  )
)
if not defined VCPKG_ROOT (
  echo vcpkg not found. Install the vcpkg component in Visual Studio, or set VCPKG_ROOT.
  exit /b 1
)

"%CMAKE%" --preset vs2022-x64 %*
if errorlevel 1 exit /b 1
"%CMAKE%" --build --preset release
if errorlevel 1 exit /b 1
