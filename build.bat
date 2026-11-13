@echo off
setlocal
set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" (
  echo CMake not found in Visual Studio 2022.
  exit /b 1
)
"%CMAKE%" --preset vs2022-x64 %*
if errorlevel 1 exit /b 1
"%CMAKE%" --build --preset release
