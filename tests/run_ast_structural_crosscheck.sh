#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TESTS_DIR="$ROOT_DIR/tests"
MANIFEST="$TESTS_DIR/manifest.json"
NODEC="${NODE_COMPILER:-$ROOT_DIR/bin/protoscriptc}"
CCLI="${C_COMPILER:-$ROOT_DIR/c/pscc}"

if ! command -v jq >/dev/null 2>&1; then
  if [[ -x "/usr/local/bin/jq" ]]; then
    PATH="/usr/local/bin:$PATH"
  elif [[ -x "/opt/local/bin/jq" ]]; then
    PATH="/opt/local/bin:$PATH"
  fi
fi
if ! command -v jq >/dev/null 2>&1; then
  echo "ERROR: jq is required." >&2
  exit 2
fi

extract_signature() {
  local in_json="$1"
  local out_sig="$2"
  node - "$in_json" "$out_sig" <<'JS'
const fs = require('fs');
const ast = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const out = process.argv[3];

const KEEP = new Set([
  'Program','FunctionDecl','Block','VarDecl','AssignStmt','ExprStmt','ReturnStmt','ThrowStmt','BreakStmt','ContinueStmt',
  'ForStmt','SwitchStmt','CaseClause','DefaultClause',
  'CallExpr','MemberExpr','IndexExpr','BinaryExpr','UnaryExpr','PostfixExpr','ConditionalExpr',
  'Literal','Identifier','ListLiteral','MapLiteral','MapPair'
]);

function collectChildrenNodeStyle(n) {
  const ordered = [];
  const keyOrder = [
    'decls','params','retType',
    'init','cond','step','iterVar','iterExpr',
    'declaredType','type','target','index',
    'expr','key','value','thenExpr','elseExpr','left','right',
    'body','stmts','cases','defaultCase',
    'callee','args','pairs'
  ];
  const seen = new Set();
  for (const k of keyOrder) {
    if (!(k in n)) continue;
    seen.add(k);
    const v = n[k];
    if (Array.isArray(v)) ordered.push(...v);
    else if (v && typeof v === 'object') ordered.push(v);
  }
  for (const k of Object.keys(n)) {
    if (seen.has(k) || k === 'kind') continue;
    const v = n[k];
    if (Array.isArray(v)) ordered.push(...v);
    else if (v && typeof v === 'object') ordered.push(v);
  }
  return ordered;
}

function normalize(n) {
  if (!n || typeof n !== 'object') return [];
  if (Array.isArray(n)) return n.flatMap(normalize);
  if (typeof n.kind !== 'string') return [];

  const rawChildren = Array.isArray(n.children) ? n.children : collectChildrenNodeStyle(n);
  const normalizedChildren = rawChildren.flatMap(normalize);
  if (!KEEP.has(n.kind)) return normalizedChildren;
  return [{ kind: n.kind, children: normalizedChildren }];
}

function preorder(node, depth, lines) {
  lines.push(`${depth}|${node.kind}|${node.children.length}`);
  for (const c of node.children) preorder(c, depth + 1, lines);
}

const forest = normalize(ast);
const lines = [];
for (const root of forest) preorder(root, 0, lines);
lines.push(`TOTAL:${lines.length}`);
fs.writeFileSync(out, lines.join('\n') + '\n');
JS
}

pass=0
fail=0

echo "== AST Structural Crosscheck (Node AST vs C AST) =="
echo "Node compiler: $NODEC"
echo "C compiler:    $CCLI"
echo

while IFS= read -r case_id; do
  [[ -z "$case_id" ]] && continue
  src="$TESTS_DIR/$case_id.pts"
  expect="$TESTS_DIR/$case_id.expect.json"
  status="$(jq -r '.status // empty' "$expect")"

  # Structural AST comparison only makes sense for parseable sources.
  if [[ "$status" == "reject-parse" ]]; then
    continue
  fi

  node_json="$(mktemp)"
  c_json="$(mktemp)"
  node_sig="$(mktemp)"
  c_sig="$(mktemp)"

  set +e
  "$NODEC" --emit-ast-json "$src" >"$node_json" 2>/dev/null
  rc_node=$?
  "$CCLI" --ast-c "$src" >"$c_json" 2>/dev/null
  rc_c=$?
  set -e

  ok=true
  reason=""

  if [[ $rc_node -ne 0 || $rc_c -ne 0 ]]; then
    ok=false
    reason="cannot emit AST (node=$rc_node c=$rc_c)"
  else
    extract_signature "$node_json" "$node_sig"
    extract_signature "$c_json" "$c_sig"
    if ! diff -u "$node_sig" "$c_sig" >/dev/null; then
      ok=false
      reason="strict structural signature differs (order/arity)"
    fi
  fi

  if [[ "$ok" == true ]]; then
    echo "PASS $case_id"
    pass=$((pass + 1))
  else
    echo "FAIL $case_id"
    echo "  $reason"
    if [[ -f "$node_sig" && -f "$c_sig" ]]; then
      diff -u "$node_sig" "$c_sig" | sed -n '1,80p' | sed 's/^/    /'
    fi
    fail=$((fail + 1))
  fi

  rm -f "$node_json" "$c_json" "$node_sig" "$c_sig"
done < <(jq -r '.suites | to_entries[] | .value[]' "$MANIFEST")

echo
echo "Summary: PASS=$pass FAIL=$fail TOTAL=$((pass + fail))"
if [[ $fail -ne 0 ]]; then
  exit 1
fi

echo "AST structural crosscheck PASSED."
