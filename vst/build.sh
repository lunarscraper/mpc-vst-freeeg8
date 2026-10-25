#!/usr/bin/env bash
# Build Free EQ8 (Gen1 armv7 and Gen2 aarch64) through mpc-vst-plugins' tools/build_port.sh.
#   MPC_VST=/path/to/mpc-vst-plugins vst/build.sh [all|armv7|aarch64]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
MPC_VST="${MPC_VST:-$REPO/../mpc-vst-plugins}"
"$MPC_VST/tools/build_port.sh" "$HERE/vst.json" "${1:-all}"
