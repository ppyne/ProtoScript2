#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

FILES=(
  "$ROOT_DIR/docs/builtin_conformance_audit_2.md"
  "$ROOT_DIR/MANUEL_REFERENCE.md"
  "$ROOT_DIR/SPECIFICATION.md"
  "$ROOT_DIR/docs/module_regexp_specification.md"
  "$ROOT_DIR/docs/module_sys_execute_specification.md"
  "$ROOT_DIR/docs/module_fs_specification.md"
  "$ROOT_DIR/docs/module_json_specification.md"
  "$ROOT_DIR/docs/module_time_specification.md"
)

rc=0
for file in "${FILES[@]}"; do
  awk '
    BEGIN { in_block = 0; lang = ""; fail = 0 }
    function trim(s) { sub(/^[[:space:]]+/, "", s); sub(/[[:space:]]+$/, "", s); return s }
    function report(msg, line) {
      printf "%s:%d: %s\n  %s\n", FILENAME, NR, msg, line
      fail = 1
    }
    /^```/ {
      if (!in_block) {
        in_block = 1
        lang = trim(tolower(substr($0, 4)))
      } else {
        in_block = 0
        lang = ""
      }
      next
    }
    {
      if (!in_block) next
      check_lang = (lang == "" || lang == "c" || lang == "protoscript" || lang == "pts" || lang == "ps")
      if (!check_lang) next
      line = $0
      if (line ~ /function[^(]*\([^)]*:[^)]*\)/) {
        report("invalid ProtoScript parameter syntax (name:type)", line)
      }
      if (line ~ /\([[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*:[[:space:]]*(int|bool|string|float|byte|glyph|void|list<[^>]+>|map<[^>]+>|[A-Z][A-Za-z0-9_<>]*)/) {
        report("invalid ProtoScript parameter syntax (name:type)", line)
      }
    }
    END { if (fail) exit 1 }
  ' "$file" || rc=1
done

if [[ "$rc" -ne 0 ]]; then
  echo "Docs ProtoScript lint FAILED" >&2
  exit 1
fi

echo "Docs ProtoScript lint PASSED"
