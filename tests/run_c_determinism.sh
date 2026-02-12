#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PSCC="${PSCC:-$ROOT_DIR/c/pscc}"

"$PSCC" --check-c-static-twice "$ROOT_DIR/tests/edge/group_stress.pts"
