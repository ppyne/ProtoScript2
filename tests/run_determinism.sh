#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

node "$ROOT_DIR/tests/robustness/determinism.js" \
  "$ROOT_DIR/tests/edge/group_stress.pts" \
  "$ROOT_DIR/tests/edge/module_import_path_basic.pts"
