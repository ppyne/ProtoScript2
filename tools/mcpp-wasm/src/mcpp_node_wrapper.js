"use strict";

const runtimeModule = require("./mcpp_core.js");

let runtime = runtimeModule;

function buildOrigToPre(preToOrigLine) {
  const origToPreLine = [0];
  for (let pre = 1; pre < preToOrigLine.length; pre += 1) {
    const orig = preToOrigLine[pre];
    if (!orig || orig <= 0) continue;
    if (!origToPreLine[orig]) origToPreLine[orig] = pre;
  }
  return origToPreLine;
}

function getRuntime() {
  if (!runtime) {
    throw new Error("mcpp runtime is unavailable");
  }
  if (typeof runtime.ccall !== "function") {
    throw new Error("mcpp runtime is not initialized synchronously");
  }
  return runtime;
}

function preprocess(source, options = {}) {
  if (typeof source !== "string") {
    throw new Error("preprocess(source, options): source must be a string");
  }
  const mod = getRuntime();
  const file = typeof options.file === "string" && options.file.length > 0 ? options.file : "<input>";

  const ok = mod.ccall("ps_mcpp_preprocess", "number", ["string", "string"], [source, file]);
  if (!ok) {
    const errPtr = mod.ccall("ps_mcpp_error", "number", [], []);
    const err = errPtr ? mod.UTF8ToString(errPtr) : "preprocessor failed";
    mod.ccall("ps_mcpp_clear", null, [], []);
    throw new Error(err || "preprocessor failed");
  }

  const outPtr = mod.ccall("ps_mcpp_output", "number", [], []);
  const code = outPtr ? mod.UTF8ToString(outPtr) : "";

  const len = mod.ccall("ps_mcpp_map_len", "number", [], []);
  const preToOrigLine = [0];
  for (let i = 0; i < len; i += 1) {
    preToOrigLine.push(mod.ccall("ps_mcpp_map_line", "number", ["number"], [i]));
  }

  mod.ccall("ps_mcpp_clear", null, [], []);

  return {
    code,
    mapping: {
      preToOrigLine,
      origToPreLine: buildOrigToPre(preToOrigLine),
    },
  };
}

module.exports = {
  preprocess,
};
