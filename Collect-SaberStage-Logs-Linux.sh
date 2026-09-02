#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
#
# Part of SaberStage.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# File responsibility:
# - Collects SaberStage support logs from a selected Quest on Linux.
# - Collection is read-only and leaves ADB stopped after the operation.

# Development launcher adapted from the Big Screen source workflow.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python3 "${root}/scripts/quest_tool.py" collect-logs "$@"
result=$?
adb kill-server >/dev/null 2>&1 || true
exit "${result}"
