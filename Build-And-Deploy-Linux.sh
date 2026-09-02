#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
#
# Part of SaberStage.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# File responsibility:
# - Builds SaberStage and safely deploys the source install from Linux.
# - Quest selection and ownership checks prevent overwriting an MBF-managed install.

# Development launcher adapted from the Big Screen source workflow.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${root}"

cleanup() {
  adb kill-server >/dev/null 2>&1 || true
}
trap cleanup EXIT

printf '%s\n' \
  '============================================================' \
  'SaberStage source build and deployment launcher' \
  '============================================================'
read -r -p 'Build QMOD only, deploy source build, or cancel [Q/D/C]: ' choice
case "${choice}" in
  q|Q) deploy=0 ;;
  d|D) deploy=1 ;;
  *) printf 'Cancelled.\n'; exit 0 ;;
esac

if ! command -v pwsh >/dev/null 2>&1; then
  printf 'PowerShell 7 is required by the current cross-platform QPM scripts.\n' >&2
  exit 1
fi
qpm scripts host-test && qpm scripts qmod || exit $?
if (( deploy == 1 )); then
  python3 "${root}/scripts/quest_tool.py" deploy || exit $?
fi
printf 'Success. ADB was stopped so ModsBeforeFriday can connect later.\n'
