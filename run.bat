@echo off
setlocal
rem DC4_ADVDATA overrides the sibling DC4\AdvData installation.
if not defined DC4_ADVDATA set "DC4_ADVDATA=%~dp0..\DC4\AdvData"
set "DC4_EXE=%~dp0build\desktop\dc4_desktop.exe"
if not exist "%DC4_EXE%" (
  echo Build first by running build.bat.
  pause
  exit /b 1
)
if /i "%~1"=="audio" goto audio
if /i "%~1"=="title" goto title
if /i "%~1"=="scene" goto scene
if /i "%~1"=="choice" goto choice
if /i "%~1"=="storychoice" goto storychoice
if /i "%~1"=="map" goto choice
if /i "%~1"=="confirm" goto confirm
if /i "%~1"=="config" goto config
if /i "%~1"=="movie" goto movie
if /i "%~1"=="prologue" goto prologue
"%DC4_EXE%" "%DC4_ADVDATA%" %*
exit /b %errorlevel%
:audio
"%DC4_EXE%" "%DC4_ADVDATA%" --testaudio
exit /b %errorlevel%
:title
"%DC4_EXE%" "%DC4_ADVDATA%" --title %2 %3 %4 %5 %6 %7 %8 %9
exit /b %errorlevel%
:choice
"%DC4_EXE%" "%DC4_ADVDATA%" --choice %2 %3 %4 %5 %6 %7 %8 %9
exit /b %errorlevel%
:storychoice
"%DC4_EXE%" "%DC4_ADVDATA%" --scene dc4_dc99990101a.mes --line 41 --choice %2 %3 %4 %5 %6 %7 %8 %9
exit /b %errorlevel%
:confirm
"%DC4_EXE%" "%DC4_ADVDATA%" --confirm %2 %3 %4 %5 %6 %7 %8 %9
exit /b %errorlevel%
:config
"%DC4_EXE%" "%DC4_ADVDATA%" --config %2 %3 %4 %5 %6 %7 %8 %9
exit /b %errorlevel%
:movie
if "%~2"=="" (
  "%DC4_EXE%" "%DC4_ADVDATA%" --movie DC4_OP1.mpg
) else (
  "%DC4_EXE%" "%DC4_ADVDATA%" --movie %2 %3 %4 %5 %6 %7 %8 %9
)
exit /b %errorlevel%
:scene
if "%~2"=="" goto default_scene
"%DC4_EXE%" "%DC4_ADVDATA%" --scene "%~2"
exit /b %errorlevel%
:default_scene
"%DC4_EXE%" "%DC4_ADVDATA%"
exit /b %errorlevel%
:prologue
"%DC4_EXE%" "%DC4_ADVDATA%" --scene DC4_SEN20181213B.mes %2 %3 %4 %5 %6 %7 %8 %9
exit /b %errorlevel%
