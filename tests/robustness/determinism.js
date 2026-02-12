#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");
const { parseAndAnalyze } = require("../../src/frontend");
const { buildIR, serializeIR } = require("../../src/ir");
const { generateC } = require("../../src/c_backend");

function compileOnce(file) {
  const full = path.resolve(file);
  const src = fs.readFileSync(full, "utf8");
  const { ast, diags } = parseAndAnalyze(full, src);
  if (diags && diags.length > 0) {
    const msg = diags.map(d => `${d.code} ${d.category} ${d.message}`).join("; ");
    throw new Error(`frontend diagnostics for ${file}: ${msg}`);
  }
  const ir = buildIR(ast);
  const irJson = JSON.stringify(serializeIR(ir));
  const cText = generateC(ir);
  return { irJson, cText };
}

function checkFile(file) {
  const first = compileOnce(file);
  const second = compileOnce(file);
  if (first.irJson !== second.irJson) {
    throw new Error(`non-deterministic IR for ${file}`);
  }
  if (first.cText !== second.cText) {
    throw new Error(`non-deterministic C output for ${file}`);
  }
  return true;
}

const files = process.argv.slice(2);
if (files.length === 0) {
  console.error("usage: determinism.js <file.pts> [file.pts ...]");
  process.exit(2);
}

let ok = true;
for (const f of files) {
  try {
    checkFile(f);
    console.log(`PASS determinism ${f}`);
  } catch (err) {
    ok = false;
    console.error(`FAIL determinism ${f}`);
    console.error(`  ${err && err.message ? err.message : String(err)}`);
  }
}

process.exit(ok ? 0 : 1);
