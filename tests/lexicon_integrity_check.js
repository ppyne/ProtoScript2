#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");

function read(rel) {
  return fs.readFileSync(path.join(root, rel), "utf8");
}

function parseKeywords(frontendJs) {
  const m = frontendJs.match(/const KEYWORDS = new Set\(\[([\s\S]*?)\]\);/);
  if (!m) return [];
  const out = [];
  const re = /"([A-Za-z_][A-Za-z0-9_]*)"/g;
  let r;
  while ((r = re.exec(m[1])) !== null) out.push(r[1]);
  return Array.from(new Set(out));
}

function parseDeclaredMethods(lexicon) {
  const out = [];
  for (const e of lexicon) {
    if (e && e.category === "method" && typeof e.symbol === "string") {
      out.push(e.symbol);
    }
  }
  return out;
}

function methodExistsInSources(symbol, frontendJs, cFrontend, runtimeJs) {
  // "string.concat()" -> method "concat"
  const dot = symbol.lastIndexOf(".");
  if (dot < 0) return false;
  const nameWithCall = symbol.slice(dot + 1);
  const name = nameWithCall.endsWith("()") ? nameWithCall.slice(0, -2) : nameWithCall;
  if (!name) return false;

  const needleQuoted = `\"${name}\"`;
  const inC = cFrontend.includes(needleQuoted) || cFrontend.includes(`'${name}'`);
  const inJs = frontendJs.includes(needleQuoted) || frontendJs.includes(`'${name}'`);
  const inRt = runtimeJs.includes(needleQuoted) || runtimeJs.includes(`'${name}'`);
  return inC || inJs || inRt;
}

function parseAugmentedStatuses(md) {
  const map = new Map();
  const sections = md.split(/^###\s+/m).slice(1);
  for (const sec of sections) {
    const lines = sec.split(/\r?\n/);
    const symbol = (lines[0] || "").trim();
    if (!symbol) continue;
    const stLine = lines.find((l) => l.startsWith("- Statut : "));
    if (!stLine) continue;
    map.set(symbol, stLine.replace("- Statut : ", "").trim());
  }
  return map;
}

function main() {
  const lexicon = JSON.parse(read("docs/lexicon.json"));
  const frontendJs = read("src/frontend.js");
  const cFrontend = read("c/frontend.c");
  const runtimeJs = read("src/runtime.js");
  const augmentedMd = read("docs/lexical_cartography_augmented.md");

  const report = {
    missingLexicalItem: [],
    statusMismatch: [],
    frontendMismatch: [],
    unclassifiedSymbol: [],
  };

  const allowedStatus = new Set(["CORE", "STDLIB", "RUNTIME_ONLY", "TEST_ONLY"]);

  // 1) All lexer keywords are listed in lexicon.
  const keywords = parseKeywords(frontendJs);
  const lexiconSymbols = new Set(lexicon.map((e) => e.symbol));
  for (const kw of keywords) {
    if (!lexiconSymbols.has(kw)) {
      report.missingLexicalItem.push({ kind: "keyword", symbol: kw, reason: "missing from docs/lexicon.json" });
    }
  }

  // 2) All declared methods exist in source frontends/runtime.
  const methods = parseDeclaredMethods(lexicon);
  for (const m of methods) {
    if (!methodExistsInSources(m, frontendJs, cFrontend, runtimeJs)) {
      report.missingLexicalItem.push({ kind: "method", symbol: m, reason: "method not found in src/frontend.js, c/frontend.c, or src/runtime.js" });
    }
  }

  // 3) No symbol has missing/invalid status.
  for (const e of lexicon) {
    if (!e || typeof e.symbol !== "string") continue;
    if (!e.status || !allowedStatus.has(e.status)) {
      report.unclassifiedSymbol.push({ symbol: e.symbol, status: e.status || null });
    }
  }

  // Additional coherence: markdown augmented status must match lexicon status.
  const augmentedStatus = parseAugmentedStatuses(augmentedMd);
  for (const e of lexicon) {
    if (!e || typeof e.symbol !== "string") continue;
    const mdStatus = augmentedStatus.get(e.symbol);
    if (mdStatus && mdStatus !== e.status) {
      report.statusMismatch.push({ symbol: e.symbol, lexiconStatus: e.status, markdownStatus: mdStatus });
    }
  }

  // 4) No CORE symbol absent from C frontend.
  for (const e of lexicon) {
    if (!e || e.status !== "CORE") continue;
    const cPresent = e.frontends && e.frontends.c === true;
    if (!cPresent) {
      report.frontendMismatch.push({
        symbol: e.symbol,
        status: e.status,
        expected: "frontends.c=true",
        actual: e.frontends,
      });
    }
  }

  process.stdout.write(JSON.stringify(report, null, 2) + "\n");

  const hasIssues =
    report.missingLexicalItem.length > 0 ||
    report.statusMismatch.length > 0 ||
    report.frontendMismatch.length > 0 ||
    report.unclassifiedSymbol.length > 0;

  process.exit(hasIssues ? 1 : 0);
}

main();
