@echo off
@REM SPDX-License-Identifier: GPL-3.0-only
@REM SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
@REM
@REM Part of SaberStage. Distributed under GPL-3.0-only with additional terms
@REM under GPLv3 section 7(b)/(c) and an interoperability permission under
@REM section 7; see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

@REM File responsibility:
@REM - Provides the Windows entry point for ownership-aware SaberStage removal.
@REM - Removal remains available while preserving explicit choices for settings and recordings.

@REM Development launcher adapted from the Big Screen source workflow.
setlocal
pushd "%~dp0"
set "SABERSTAGE_ADB=C:\Users\Owner\AppData\Local\Programs\QPM\platform-tools\adb.exe"
echo This removes only the receipt-owned SaberStage source build.
echo It does not delete settings, logs, recordings, or unrelated mods.
python "%~dp0scripts\quest_tool.py" remove
set "SABERSTAGE_RESULT=%ERRORLEVEL%"
if exist "%SABERSTAGE_ADB%" "%SABERSTAGE_ADB%" kill-server >nul 2>nul
echo ADB was stopped so ModsBeforeFriday can connect.
pause
popd
exit /b %SABERSTAGE_RESULT%
