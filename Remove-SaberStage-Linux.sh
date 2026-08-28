#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Development launcher adapted from the Big Screen source workflow.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
printf 'This removes only the receipt-owned SaberStage source build.\n'
python3 "${root}/scripts/quest_tool.py" remove
result=$?
adb kill-server >/dev/null 2>&1 || true
printf 'ADB was stopped so ModsBeforeFriday can connect.\n'
exit "${result}"
