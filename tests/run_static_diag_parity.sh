#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANIFEST="$ROOT_DIR/tests/manifest.json"
NODE_COMPILER="${COMPILER:-$ROOT_DIR/bin/protoscriptc}"
C_CLI="${PS:-$ROOT_DIR/c/ps}"

echo "== Static Diagnostic Parity (JS/C) =="
echo "Node: $NODE_COMPILER"
echo "C:    $C_CLI"
echo

python3 - "$ROOT_DIR" "$MANIFEST" "$NODE_COMPILER" "$C_CLI" <<'PY'
import json
import re
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
manifest_path = Path(sys.argv[2])
node_compiler = sys.argv[3]
c_cli = sys.argv[4]

diag_re = re.compile(r"^(.*?):(\d+):(\d+)\s+(E\d{4})\s+", re.MULTILINE)

def parse_diags(text):
    out = []
    for m in diag_re.finditer(text):
        out.append((int(m.group(2)), int(m.group(3)), m.group(4)))
    return out

with manifest_path.open("r", encoding="utf-8") as f:
    manifest = json.load(f)

suites = manifest.get("suites", {})
cases = []
for suite_name, entries in suites.items():
    if not suite_name.startswith("invalid/"):
        continue
    if suite_name == "invalid/runtime":
        continue
    for case in entries:
        cases.append(case)

pass_count = 0
fail_count = 0
for case in cases:
    src = root / "tests" / f"{case}.pts"
    if not src.exists():
        print(f"FAIL {case}")
        print(f"  missing file: {src}")
        fail_count += 1
        continue
    node = subprocess.run([node_compiler, "--check", str(src)], capture_output=True, text=True)
    c = subprocess.run([c_cli, "check", str(src)], capture_output=True, text=True)
    node_diags = parse_diags(node.stdout + node.stderr)
    c_diags = parse_diags(c.stdout + c.stderr)
    if node_diags == c_diags:
        print(f"PASS {case}")
        pass_count += 1
    else:
        print(f"FAIL {case}")
        print(f"  node: {node_diags}")
        print(f"  c:    {c_diags}")
        fail_count += 1

print()
print(f"Summary: PASS={pass_count} FAIL={fail_count} TOTAL={pass_count + fail_count}")
if fail_count:
    sys.exit(1)
PY
