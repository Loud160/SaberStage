@echo off
@REM SPDX-License-Identifier: GPL-3.0-only
@REM SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
@REM
@REM Part of SaberStage. Distributed under GPL-3.0-only with additional terms
@REM under GPLv3 section 7(b)/(c) and an interoperability permission under
@REM section 7; see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

@REM File responsibility:
@REM - Collects SaberStage support logs from a selected Quest on Windows.
@REM - The wrapper uses the shared Quest tool so device selection matches deployment.

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
