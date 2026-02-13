"use strict";

const RESERVED_RANGES = Object.freeze({
  E1XXX: "PARSE",
  E2XXX: "NAME_RESOLUTION",
  E3XXX: "TYPE",
  E4XXX: "SEMANTIC",
  E5XXX: "MODULE_IMPORT",
  E6XXX: "STATIC_RUNTIME_CHECK",
});

function normalizeLoc(line, column) {
  const l = Number.isInteger(line) && line > 0 ? line : 1;
  const c = Number.isInteger(column) && column > 0 ? column : 1;
  return { line: l, column: c };
}

function uniqueSorted(items) {
  const arr = Array.isArray(items) ? items : Array.from(items || []);
  return [...new Set(arr.filter((s) => typeof s === "string" && s.length > 0))].sort();
}

function levenshtein(a, b) {
  const s = String(a || "");
  const t = String(b || "");
  const n = s.length;
  const m = t.length;
  if (n === 0) return m;
  if (m === 0) return n;
  const dp = new Array(m + 1);
  for (let j = 0; j <= m; j += 1) dp[j] = j;
  for (let i = 1; i <= n; i += 1) {
    let prev = dp[0];
    dp[0] = i;
    for (let j = 1; j <= m; j += 1) {
      const tmp = dp[j];
      const cost = s[i - 1] === t[j - 1] ? 0 : 1;
      dp[j] = Math.min(dp[j] + 1, dp[j - 1] + 1, prev + cost);
      prev = tmp;
    }
  }
  return dp[m];
}

function pickSuggestions(query, candidates, maxDistance = 2) {
  const q = String(query || "");
  if (!q) return [];
  const pool = uniqueSorted(candidates).filter((c) => c !== q);
  if (pool.length === 0) return [];
  const scored = [];
  for (const c of pool) {
    const dist = levenshtein(q, c);
    if (dist <= maxDistance) scored.push({ c, dist });
  }
  if (scored.length === 0) return [];
  scored.sort((a, b) => (a.dist - b.dist) || a.c.localeCompare(b.c));
  const bestDist = scored[0].dist;
  const best = scored.filter((s) => s.dist === bestDist).map((s) => s.c);
  if (best.length === 1) return best;
  if (best.length === 2) return best;
  return [];
}

function createDiagnostic({
  file,
  line,
  column,
  code,
  name,
  message,
  expectedKind = null,
  actualKind = null,
  suggestions = [],
}) {
  const loc = normalizeLoc(line, column);
  return {
    file: file || "<unknown>",
    line: loc.line,
    col: loc.column,
    column: loc.column,
    code: code || "E0002",
    name: name || "INTERNAL_ERROR",
    category: name || "INTERNAL_ERROR",
    message: message || "internal error",
    expected_kind: expectedKind || undefined,
    actual_kind: actualKind || undefined,
    suggestions: uniqueSorted(suggestions),
  };
}

function formatDiagnostic(d) {
  const head = `${d.file}:${d.line}:${d.col} ${d.code} ${d.name || d.category}: ${d.message}`;
  const sugg = Array.isArray(d.suggestions) ? uniqueSorted(d.suggestions) : [];
  if (sugg.length === 0) return head;
  if (sugg.length === 1) return `${head}\nDid you mean '${sugg[0]}'?`;
  return `${head}\nDid you mean '${sugg[0]}' or '${sugg[1]}'?`;
}

module.exports = {
  RESERVED_RANGES,
  createDiagnostic,
  formatDiagnostic,
  levenshtein,
  pickSuggestions,
};
