#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
declare -a SOURCES=(
  "$ROOT_DIR/c/cli/ps.c"
)

echo "== CLI Autonomy Guard =="

declare -a banned_patterns=(
  "\\bexecv\\b"
  "\\bsystem\\b"
  "\\bpopen\\b"
  "\\bposix_spawn\\b"
  "protoscriptc"
  "\\bnode\\b"
  "/bin/"
)

for src in "${SOURCES[@]}"; do
  if [[ ! -f "$src" ]]; then
    echo "ERROR: missing CLI source: $src" >&2
    exit 2
  fi
  echo "file: $src"
  for pattern in "${banned_patterns[@]}"; do
    if rg -n -i "$pattern" "$src" >/tmp/ps_cli_autonomy_guard.out 2>&1; then
      echo "FAIL: forbidden delegation marker detected: $pattern"
      sed -n '1,40p' /tmp/ps_cli_autonomy_guard.out
      exit 1
    fi
  done
done

echo "CLI autonomy guard PASSED."
