#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Development launcher adapted from the Big Screen source workflow.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python3 "${root}/scripts/quest_tool.py" collect-logs "$@"
result=$?
adb kill-server >/dev/null 2>&1 || true
exit "${result}"
