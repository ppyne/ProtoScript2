#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "== Orchestrator Prerequisite Guard =="
"$ROOT_DIR/tools/test-orchestrator" --selftest-prereq-guard
