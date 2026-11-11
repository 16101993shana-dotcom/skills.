@echo off
rem Runs every Chronopolis automation test headless, including the
rem zero-clipping audit. A phase is only valid if this passes.
rem Usage: set UE_ROOT to the engine folder, then run this script.
rem   set UE_ROOT=C:\Program Files\Epic Games\UE_5.8

if "%UE_ROOT%"=="" (
  echo UE_ROOT is not set.
  exit /b 2
)

"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\..\Chronopolis.uproject" ^
  -ExecCmds="Automation RunTests Chronopolis; Quit" ^
  -TestExit="Automation Test Queue Empty" ^
  -ReportExportPath="%~dp0..\..\Saved\AutomationReports" ^
  -unattended -nopause -nosplash -nullrhi -log

exit /b %ERRORLEVEL%
