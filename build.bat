@echo off
setlocal
rem Build in a fresh subdirectory; an older relocated build/ cache is untouched.
set "DC4_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%DC4_VSWHERE%" (
  echo Visual Studio C++ Build Tools with the CMake tools component are required.
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%DC4_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DC4_VS=%%i"
if not defined DC4_VS (
  echo Visual Studio C++ Build Tools were not found.
  exit /b 1
)
call "%DC4_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "DC4_CMAKE=%DC4_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "DC4_NINJA=%DC4_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%DC4_CMAKE%" (
  echo Install the C++ CMake tools for Windows component in Visual Studio Installer.
  exit /b 1
)
if not exist "%DC4_NINJA%" (
  echo Install the C++ CMake tools for Windows component in Visual Studio Installer.
  exit /b 1
)
"%DC4_CMAKE%" -S "%~dp0." -B "%~dp0build\desktop" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=%DC4_NINJA%"
if errorlevel 1 exit /b 1
"%DC4_CMAKE%" --build "%~dp0build\desktop" --parallel %*
exit /b %errorlevel%
