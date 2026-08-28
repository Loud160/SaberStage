@echo off
@REM SPDX-License-Identifier: GPL-3.0-only
@REM Development launcher adapted from the Big Screen source workflow.
setlocal
pushd "%~dp0"
set "SABERSTAGE_ADB=C:\Users\Owner\AppData\Local\Programs\QPM\platform-tools\adb.exe"
python "%~dp0scripts\quest_tool.py" collect-logs %*
set "SABERSTAGE_RESULT=%ERRORLEVEL%"
if exist "%SABERSTAGE_ADB%" "%SABERSTAGE_ADB%" kill-server >nul 2>nul
echo.
if "%SABERSTAGE_RESULT%"=="0" (echo Log collection completed.) else (echo Log collection failed.)
pause
popd
exit /b %SABERSTAGE_RESULT%
