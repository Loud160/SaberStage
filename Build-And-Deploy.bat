@echo off
@REM SPDX-License-Identifier: GPL-3.0-only
@REM SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
@REM
@REM Part of SaberStage. Distributed under GPL-3.0-only with additional terms
@REM under GPLv3 section 7(b)/(c) and an interoperability permission under
@REM section 7; see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

@REM File responsibility:
@REM - Provides the Windows one-click build and source-deploy entry point.
@REM - It delegates repository logic to shared scripts and keeps Windows ADB handling explicit.

@REM Development launcher adapted from the Big Screen source workflow.
setlocal EnableExtensions
pushd "%~dp0"

set "SABERSTAGE_ADB=C:\Users\Owner\AppData\Local\Programs\QPM\platform-tools\adb.exe"
set "SABERSTAGE_RESULT=0"

echo ============================================================
echo SaberStage source build and deployment launcher
echo ============================================================
echo   [Q] Build and package SaberStage.qmod only
echo   [D] Build, package, and install the source build on Quest
echo   [C] Cancel
choice /C QDC /N /M "Select QMOD only, direct deployment, or cancel [Q/D/C]: "
if errorlevel 3 goto :cancelled
if errorlevel 2 set "SABERSTAGE_DEPLOY=1"

echo.
echo Running host tests, Quest build, and QMOD packaging...
call powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'C:\Users\Owner\AppData\Local\Programs\QPM\qpm.exe' scripts host-test; if ($LASTEXITCODE) { exit $LASTEXITCODE }; & 'C:\Users\Owner\AppData\Local\Programs\QPM\qpm.exe' scripts qmod"
set "SABERSTAGE_RESULT=%ERRORLEVEL%"
if not "%SABERSTAGE_RESULT%"=="0" goto :failure
if not defined SABERSTAGE_DEPLOY goto :success

if not exist "%SABERSTAGE_ADB%" (
  echo ERROR: QPM ADB was not found at %SABERSTAGE_ADB%
  set "SABERSTAGE_RESULT=1"
  goto :failure
)
echo.
echo Installing the verified source build with an ownership receipt...
python "%~dp0scripts\quest_tool.py" deploy
set "SABERSTAGE_RESULT=%ERRORLEVEL%"
if not "%SABERSTAGE_RESULT%"=="0" goto :failure
goto :success

:cancelled
echo Cancelled. Nothing was built or installed.
goto :finish

:success
echo.
echo SUCCESS - SaberStage.qmod was built successfully.
if defined SABERSTAGE_DEPLOY echo The receipt-owned source build was installed and Beat Saber was restarted.
goto :finish

:failure
echo.
echo BUILD OR DEPLOY FAILED - error code %SABERSTAGE_RESULT%

:finish
if exist "%SABERSTAGE_ADB%" "%SABERSTAGE_ADB%" kill-server >nul 2>nul
echo ADB was stopped so ModsBeforeFriday can connect later.
pause
popd
exit /b %SABERSTAGE_RESULT%
