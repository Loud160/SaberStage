#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
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
