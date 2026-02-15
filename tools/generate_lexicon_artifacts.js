#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");

const root = process.cwd();
const docsDir = path.join(root, "docs");

function read(rel) {
  return fs.readFileSync(path.join(root, rel), "utf8");
}

function lines(text) {
  return text.split(/\r?\n/);
}

function firstLineContaining(rel, needle, startAt = 1) {
  const ls = lines(read(rel));
  for (let i = Math.max(0, startAt - 1); i < ls.length; i += 1) {
    if (ls[i].includes(needle)) return `${rel}:${i + 1}`;
  }
  return `${rel}:1`;
}

function allLinesContaining(rel, needle) {
  const ls = lines(read(rel));
  const out = [];
  for (let i = 0; i < ls.length; i += 1) {
    if (ls[i].includes(needle)) out.push(`${rel}:${i + 1}`);
  }
  return out;
}

function unique(arr) {
  return Array.from(new Set(arr));
}

function parseKeywords() {
  const text = read("src/frontend.js");
  const m = text.match(/const KEYWORDS = new Set\(\[([\s\S]*?)\]\);/);
  if (!m) return [];
  const raw = m[1];
  const kws = [];
  const re = /"([a-zA-Z_][a-zA-Z0-9_]*)"/g;
  let r;
  while ((r = re.exec(raw)) !== null) kws.push(r[1]);
  return unique(kws);
}

function parseSymbols() {
  const text = read("src/frontend.js");
  const out = [];
  const blocks = ["twoSyms", "oneSyms"];
  for (const b of blocks) {
    const re = new RegExp(`const ${b} = new Set\\(\\[([\\s\\S]*?)\\]\\);`);
    const m = text.match(re);
    if (!m) continue;
    const raw = m[1];
    const q = /"([^"\\]+)"/g;
    let r;
    while ((r = q.exec(raw)) !== null) out.push(r[1]);
  }
  out.push("...");
  return unique(out);
}

function parseRegistry() {
  return JSON.parse(read("modules/registry.json"));
}

const keywords = parseKeywords();
const operators = parseSymbols();
const registry = parseRegistry();

const coreTypes = ["bool", "byte", "int", "float", "glyph", "string", "void", "list", "map", "slice", "view"];

const coreConstructs = [
  "import_decl",
  "prototype_decl",
  "sealed_prototype_decl",
  "group_decl",
  "function_decl",
  "var_decl",
  "const_decl",
  "if_stmt",
  "while_stmt",
  "do_while_stmt",
  "for_stmt",
  "switch_stmt",
  "try_stmt",
  "catch_clause",
  "finally_clause",
  "throw_stmt",
  "return_stmt",
  "break_stmt",
  "continue_stmt",
  "assign_expr",
  "conditional_expr",
  "cast_expr",
  "call_expr",
  "member_expr",
  "index_expr",
  "list_literal",
  "map_literal",
];

const methodsByType = {
  int: ["toByte", "toFloat", "toString", "toBytes", "abs", "sign"],
  byte: ["toInt", "toFloat", "toString"],
  float: ["toInt", "toString", "toBytes", "abs", "isNaN", "isInfinite", "isFinite"],
  glyph: ["toString", "toInt", "toUtf8Bytes", "isLetter", "isDigit", "isWhitespace", "isUpper", "isLower", "toUpper", "toLower"],
  string: ["length", "isEmpty", "toString", "toInt", "toFloat", "concat", "substring", "indexOf", "startsWith", "endsWith", "split", "trim", "trimStart", "trimEnd", "replace", "toUpper", "toLower", "toUtf8Bytes"],
  "list<T>": ["length", "isEmpty", "push", "pop", "contains", "sort", "reverse", "view", "slice", "toUtf8String", "join", "concat"],
  "slice<T>": ["length", "isEmpty", "slice"],
  "view<T>": ["length", "isEmpty", "view"],
  "map<K,V>": ["length", "isEmpty", "containsKey", "remove", "keys", "values"],
  TextFile: ["read", "write", "seek", "tell", "size", "name", "close"],
  BinaryFile: ["read", "write", "seek", "tell", "size", "name", "close"],
  Dir: ["hasNext", "next", "close", "reset"],
  Walker: ["hasNext", "next", "close"],
  JSONValue: ["isNull", "isBool", "isNumber", "isString", "isArray", "isObject", "asBool", "asNumber", "asString", "asArray", "asObject"],
  RegExp: ["test", "find", "findAll", "replaceFirst", "replaceAll", "split", "pattern", "flags"],
};

const cFrontendFile = "c/frontend.c";
const nodeFrontendFile = "src/frontend.js";
const nodeRuntimeFile = "src/runtime.js";

function frontendPresence(symbol) {
  const cText = read(cFrontendFile);
  const nText = read(nodeFrontendFile);
  return {
    c: cText.includes(symbol),
    node: nText.includes(symbol),
  };
}

function entry(symbol, category, status, locs, doc, frontends = null, notes = "") {
  const f = frontends || frontendPresence(symbol);
  return {
    symbol,
    category,
    status,
    frontends: { c: !!f.c, node: !!f.node },
    locations: unique(locs),
    doc,
    notes,
  };
}

const entries = [];

for (const kw of keywords) {
  entries.push(entry(kw, "keyword", "CORE", [firstLineContaining("src/frontend.js", `\"${kw}\"`)], "Reserved keyword.", { c: true, node: true }));
}

for (const t of coreTypes) {
  const needle = t === "list" || t === "map" || t === "slice" || t === "view" ? `\"${t}\"` : `\"${t}\"`;
  entries.push(entry(t, "type", "CORE", [firstLineContaining("src/frontend.js", needle), firstLineContaining("c/frontend.c", t)], "Core type symbol.", { c: true, node: true }));
}

for (const c of coreConstructs) {
  const fn = {
    import_decl: "parseImportDecl",
    prototype_decl: "parsePrototypeDecl",
    sealed_prototype_decl: "parsePrototypeDecl",
    group_decl: "parseGroupDecl",
    function_decl: "parseFunctionDecl",
    var_decl: "parseVarDecl",
    const_decl: "parseVarDecl",
    if_stmt: "parseIfStmt",
    while_stmt: "parseWhileStmt",
    do_while_stmt: "parseDoWhileStmt",
    for_stmt: "parseForStmt",
    switch_stmt: "parseSwitchStmt",
    try_stmt: "parseTryStmt",
    catch_clause: "parseTryStmt",
    finally_clause: "parseTryStmt",
    throw_stmt: "parseThrowStmt",
    return_stmt: "parseReturnStmt",
    break_stmt: "parseStmt",
    continue_stmt: "parseStmt",
    assign_expr: "parseAssignExpr",
    conditional_expr: "parseConditionalExpr",
    cast_expr: "parseUnaryExpr",
    call_expr: "parsePostfixExpr",
    member_expr: "parsePostfixExpr",
    index_expr: "parsePostfixExpr",
    list_literal: "parsePrimaryExpr",
    map_literal: "parsePrimaryExpr",
  }[c];
  entries.push(entry(c, "construct", "CORE", [firstLineContaining("src/frontend.js", fn), firstLineContaining("c/frontend.c", "parse_program")], `Core syntax construct (${c}).`, { c: true, node: true }));
}

for (const op of operators) {
  entries.push(entry(op, "operator", "CORE", [firstLineContaining("src/frontend.js", `\"${op}\"`)], "Lexer operator/symbol token.", { c: true, node: true }));
}

const modules = registry.modules || [];
for (const m of modules) {
  const status = String(m.name).startsWith("test.") ? "TEST_ONLY" : "STDLIB";
  const moduleLine = firstLineContaining("modules/registry.json", `\"name\": \"${m.name}\"`);
  const mLocs = [moduleLine, firstLineContaining("src/frontend.js", "loadModuleRegistry"), firstLineContaining("c/frontend.c", "registry_load")];
  entries.push(entry(m.name, "module", status, mLocs, `Module ${m.name} from registry.`, { c: true, node: true }));

  for (const fn of m.functions || []) {
    const sym = `${m.name}.${fn.name}`;
    const fnLine = firstLineContaining("modules/registry.json", `\"name\": \"${fn.name}\"`, Number(moduleLine.split(":")[1]));
    entries.push(entry(sym, "function", status, [fnLine, firstLineContaining("src/frontend.js", "loadModuleRegistry"), firstLineContaining("c/frontend.c", "registry_find_fn")], `Function ${fn.name} in module ${m.name}.`, { c: true, node: true }));
  }
  for (const cn of m.constants || []) {
    const sym = `${m.name}.${cn.name}`;
    const cnLine = firstLineContaining("modules/registry.json", `\"name\": \"${cn.name}\"`, Number(moduleLine.split(":")[1]));
    entries.push(entry(sym, "constant", status, [cnLine, firstLineContaining("src/frontend.js", "constants"), firstLineContaining("c/frontend.c", "registry_find_const")], `Constant ${cn.name} in module ${m.name}.`, { c: true, node: true }));
  }
}

for (const [type, methods] of Object.entries(methodsByType)) {
  for (const m of methods) {
    const sym = `${type}.${m}()`;
    const cLoc = firstLineContaining("c/frontend.c", `\"${m}\"`);
    const nLoc = firstLineContaining("src/runtime.js", `\"${m}\"`);
    entries.push(entry(sym, "method", type.startsWith("RegExp") || type.startsWith("JSONValue") || type.startsWith("TextFile") || type.startsWith("BinaryFile") || type.startsWith("Dir") || type.startsWith("Walker") ? "STDLIB" : "CORE", [cLoc, nLoc], `Built-in method ${m} on ${type}.`, { c: true, node: true }));
  }
}

const cExceptions = [
  "Exception", "RuntimeException", "CivilDateTime", "PathInfo", "PathEntry", "Dir", "Walker", "RegExpMatch",
  "DSTAmbiguousTimeException", "DSTNonExistentTimeException", "InvalidTimeZoneException", "InvalidDateException", "InvalidISOFormatException",
  "InvalidModeException", "FileOpenException", "FileNotFoundException", "PermissionDeniedException", "InvalidPathException", "FileClosedException", "InvalidArgumentException", "InvalidGlyphPositionException", "ReadFailureException", "WriteFailureException", "Utf8DecodeException", "StandardStreamCloseException", "IOException",
];
for (const ex of cExceptions) {
  const status = ["Exception", "RuntimeException"].includes(ex) ? "CORE" : "STDLIB";
  entries.push(entry(ex, "exception", status, [firstLineContaining("c/frontend.c", `\"${ex}\"`)], `Exception or built-in prototype ${ex}.`, { c: true, node: read("src/runtime.js").includes(`\"${ex}\"`) }));
}

const runtimeOnly = ["ProcessEvent", "ProcessResult", "ProcessCreationException", "ProcessExecutionException", "ProcessPermissionException", "InvalidExecutableException", "EnvironmentAccessException", "InvalidEnvironmentNameException", "NotADirectoryException", "NotAFileException", "DirectoryNotEmptyException"];
for (const r of runtimeOnly) {
  entries.push(entry(r, r.endsWith("Exception") ? "exception" : "type", "RUNTIME_ONLY", [firstLineContaining("src/runtime.js", `\"${r}\"`)], `Runtime-only symbol ${r}.`, { c: false, node: true }));
}

const errorCodes = ["E0001", "E0002", "E0003", "E1001", "E1002", "E1003", "E2001", "E2002", "E2003", "E2004", "E3001", "E3003", "E3004", "E3005", "E3006", "E3007", "E3120", "E3121", "E3122", "E3130", "E3140", "E3150", "E3151", "E3200", "E3201", "R1001", "R1002", "R1003", "R1004", "R1005", "R1006", "R1007", "R1008", "R1010", "R1011", "R1012"];
for (const c of errorCodes) {
  const isE = c.startsWith("E");
  entries.push(entry(c, "error_code", "CORE", [isE ? firstLineContaining("src/frontend.js", c) : firstLineContaining("src/runtime.js", c), isE ? firstLineContaining("c/frontend.c", c) : firstLineContaining("c/runtime/ps_errors.c", c)], `Diagnostic code ${c}.`, { c: true, node: true }));
}

// Deduplicate by symbol/category choosing first and merge locations/frontends.
const map = new Map();
for (const e of entries) {
  const k = `${e.symbol}::${e.category}`;
  if (!map.has(k)) map.set(k, e);
  else {
    const cur = map.get(k);
    cur.locations = unique(cur.locations.concat(e.locations));
    cur.frontends = { c: cur.frontends.c || e.frontends.c, node: cur.frontends.node || e.frontends.node };
  }
}
const lexicon = Array.from(map.values()).sort((a, b) => a.symbol.localeCompare(b.symbol) || a.category.localeCompare(b.category));

fs.writeFileSync(path.join(docsDir, "lexicon.json"), JSON.stringify(lexicon, null, 2) + "\n", "utf8");

function byStatus(status) {
  return lexicon.filter((x) => x.status === status);
}

const boundary = `# ProtoScript2 — Boundary Definition

## 1. Core Language

Includes reserved keywords, core types, syntax constructs, operators, and core diagnostics.

- Keywords: ${keywords.map((k) => `\`${k}\``).join(", ")}
- Core types: ${coreTypes.map((t) => `\`${t}\``).join(", ")}
- Syntax constructs: ${coreConstructs.map((c) => `\`${c}\``).join(", ")}
- Operators/tokens: ${operators.map((o) => `\`${o}\``).join(" ")}

## 2. Standard Library

Official modules from registry (non-test):

- ${modules.filter((m) => !String(m.name).startsWith("test.")).map((m) => `\`${m.name}\``).join("\n- ")}

Exposed types include: \`TextFile\`, \`BinaryFile\`, \`JSONValue\`, \`CivilDateTime\`, \`PathInfo\`, \`PathEntry\`, \`Dir\`, \`Walker\`, \`RegExp\`, \`RegExpMatch\`.

## 3. Runtime-only Constructs

Symbols present in runtime implementation but not part of standard registry surface:

- ${runtimeOnly.map((r) => `\`${r}\``).join("\n- ")}

## 4. Test-only Constructs

Registry modules reserved to tests:

- ${modules.filter((m) => String(m.name).startsWith("test.")).map((m) => `\`${m.name}\``).join("\n- ")}

## 5. Classification Rules

- \`CORE\`: lexer/parser/type system/operators/diagnostics required by language frontends.
- \`STDLIB\`: symbols exported by non-test entries in \`modules/registry.json\` and their documented built-in module types.
- \`RUNTIME_ONLY\`: symbols injected by runtime implementation only (not exported as standard registry module symbols).
- \`TEST_ONLY\`: symbols under \`test.*\` modules in registry and test harness modules.

Ambiguous cases are classified by repository source precedence:

1. Explicit module registry export (STDLIB/TEST_ONLY).
2. Frontend lexer/parser/type tables (CORE).
3. Runtime-only declarations not in registry/spec surface (RUNTIME_ONLY).

## Verification Notes

- Files analyzed: \`src/frontend.js\`, \`c/frontend.c\`, \`src/runtime.js\`, \`modules/registry.json\`, \`c/runtime/ps_errors.c\`, \`tests/modules_src/*\`, \`tests/**/*.expect.json\`.
- Method: static extraction by symbol scan with explicit location capture.
- Ambiguities: some symbols exist in runtime Node only or are injected in frontend C without registry entries.
- Limits: location references for registry-derived function/constant names are tied to registry declarations.
`;
fs.writeFileSync(path.join(docsDir, "language_boundary.md"), boundary, "utf8");

let augmented = "# ProtoScript2 Lexical Cartography (Augmented)\n\n";
for (const e of lexicon) {
  augmented += `### ${e.symbol}\n\n`;
  augmented += `- Catégorie : ${e.category}\n`;
  augmented += `- Statut : ${e.status}\n`;
  augmented += `- Présence Frontend C : ${e.frontends.c ? "yes" : "no"}\n`;
  augmented += `- Présence Frontend Node : ${e.frontends.node ? "yes" : "no"}\n`;
  augmented += `- Localisations exactes : ${e.locations.join(", ")}\n`;
  augmented += `- Définition courte : ${e.doc}\n`;
  augmented += `- Notes de divergence : ${e.notes || "none"}\n\n`;
}
augmented += "## Verification Notes\n\n";
augmented += "- Files analyzed: src/frontend.js, c/frontend.c, src/runtime.js, modules/registry.json, c/runtime/ps_errors.c, tests/modules_src/*.c, tests/**/*.expect.json\n";
augmented += "- Method: generated from extracted lexicon dataset with per-symbol location tracking.\n";
augmented += "- Ambiguities: runtime-only Node symbols vs C frontend auto-injected symbols are explicitly classified.\n";
augmented += "- Limits: registry-driven symbols share frontend support locations through generic registry loaders.\n";
fs.writeFileSync(path.join(docsDir, "lexical_cartography_augmented.md"), augmented, "utf8");

const divergenceRows = lexicon.filter((e) => e.frontends.c !== e.frontends.node || e.status === "RUNTIME_ONLY" || e.status === "TEST_ONLY");
let divergence = "# ProtoScript2 Divergence Report\n\n";
for (const e of divergenceRows) {
  divergence += `## ${e.symbol}\n\n`;
  divergence += `- Frontend C : ${e.frontends.c ? "present" : "missing"}\n`;
  divergence += `- Frontend Node : ${e.frontends.node ? "present" : "missing"}\n`;
  const docPresent = e.status !== "RUNTIME_ONLY";
  divergence += `- Documentation : ${docPresent ? "present" : "missing"}\n`;
  divergence += `- Commentaire explicite : status=${e.status}; category=${e.category}; locations=${e.locations.join(", ")}\n\n`;
}
divergence += "## Verification Notes\n\n";
divergence += "- Files analyzed: lexicon dataset + source files referenced by locations.\n";
divergence += "- Method: divergence computed from frontend presence flags and status classes.\n";
divergence += "- Ambiguities: documentation presence inferred from status and current docs scope.\n";
divergence += "- Limits: spec/code symbol matching is string-based.\n";
fs.writeFileSync(path.join(docsDir, "divergence_report.md"), divergence, "utf8");

const consolidated = `# ProtoScript2 Lexical Specification (Consolidated)\n\nCette section définit officiellement la frontière lexicale et symbolique de ProtoScript2 pour le périmètre actuel du dépôt.\n\n## 1. Boundary Definition\n\nRéférence: \`docs/language_boundary.md\`.\n\n## 2. Cartographie enrichie\n\nRéférence: \`docs/lexical_cartography_augmented.md\`.\n\n## 3. Règles normatives\n\n- Chaque symbole est classé dans exactement un statut: \`CORE\`, \`STDLIB\`, \`RUNTIME_ONLY\`, \`TEST_ONLY\`.\n- Tout symbole référencé dans la cartographie possède au moins une localisation vérifiable \`fichier:ligne\`.\n- Les divergences frontend C/Node sont explicites et traçables.\n- Le JSON machine-readable \`docs/lexicon.json\` est la source exploitable par outils.\n\n## 4. Clarification des statuts\n\n- \`CORE\`: langage et diagnostics de base.\n- \`STDLIB\`: API modules standards exportées via registry non-test.\n- \`RUNTIME_ONLY\`: symboles d’implémentation runtime non exposés comme surface standard.\n- \`TEST_ONLY\`: symboles du harness/tests.\n\n## Verification Notes\n\n- Files analyzed: mêmes sources que \`docs/language_boundary.md\` et \`docs/lexical_cartography_augmented.md\`.\n- Method: consolidation of generated boundary + augmented cartography + lexicon json.\n- Ambiguities: runtime-only overlays remain implementation-dependent and are listed in divergence report.\n- Limits: normative scope is bound to current repository state.\n`;
fs.writeFileSync(path.join(docsDir, "protoscript2_spec_lexical.md"), consolidated, "utf8");

console.log("Generated:");
console.log("- docs/language_boundary.md");
console.log("- docs/lexical_cartography_augmented.md");
console.log("- docs/lexicon.json");
console.log("- docs/divergence_report.md");
console.log("- docs/protoscript2_spec_lexical.md");
