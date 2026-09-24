#!/usr/bin/env bash
# =============================================================================
# test-native.sh - StackChan-Companion: runs the native unit tests (Linux)
# =============================================================================
# Usage:  ./scripts/gates/test-native.sh              (all suites)
#         ./scripts/gates/test-native.sh test_units   (one suite, -f filter)
#
# The PowerShell twin exists mostly to put MinGW on the PATH, because WinLibs is
# installed by winget WITHOUT being added to it. On Linux the host gcc is
# already there, so this wrapper is thin on purpose - it checks that it really
# is, because the failure `pio` gives otherwise names a missing toolchain
# somewhere deep in a build log rather than at the point you can fix it.
# =============================================================================
set -euo pipefail

if ! command -v gcc >/dev/null 2>&1; then
  echo "gcc introuvable - installer le compilateur hote (build-essential, base-devel...)" >&2
  exit 1
fi

PIO="$HOME/.platformio/penv/bin/pio"
[ -x "$PIO" ] || PIO="$(command -v pio)"

if [ $# -ge 1 ]; then
  exec "$PIO" test -e native -f "$1"
fi
exec "$PIO" test -e native
