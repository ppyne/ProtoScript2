#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");
const childProcess = require("child_process");

const extRoot = path.resolve(__dirname, "..");
const repoRoot = path.resolve(extRoot, "../../..");
const distDir = path.join(extRoot, "dist");
const mcppOut = path.join(repoRoot, "tools", "mcpp-wasm", "out");

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function run(cmd, args) {
  const r = childProcess.spawnSync(cmd, args, {
    cwd: extRoot,
    stdio: "inherit",
    env: process.env
  });
  if (r.status !== 0) {
    throw new Error(`${cmd} ${args.join(" ")} failed with status ${String(r.status)}`);
  }
}

function copyFileOrFail(src, dst) {
  if (!fs.existsSync(src)) {
    throw new Error(`Missing required file: ${src}`);
  }
  ensureDir(path.dirname(dst));
  fs.copyFileSync(src, dst);
}

function tryLoadEsbuild() {
  try {
    return require("esbuild");
  } catch {
    return null;
  }
}

async function buildWithEsbuild(esbuild) {
  await esbuild.build({
    entryPoints: [path.join(extRoot, "src", "extension.ts")],
    bundle: true,
    platform: "node",
    format: "cjs",
    sourcemap: true,
    target: ["node18"],
    outfile: path.join(distDir, "extension.js"),
    external: ["vscode", "vscode-languageclient/node"]
  });

  await esbuild.build({
    entryPoints: [path.join(extRoot, "src", "server.ts")],
    bundle: true,
    platform: "node",
    format: "cjs",
    sourcemap: true,
    target: ["node18"],
    outfile: path.join(distDir, "server.js"),
    external: ["vscode-languageserver/node", "vscode-languageserver-textdocument"]
  });

  await esbuild.build({
    entryPoints: [path.join(repoRoot, "src", "frontend.js")],
    bundle: true,
    platform: "node",
    format: "cjs",
    sourcemap: true,
    target: ["node18"],
    outfile: path.join(distDir, "frontend.js")
  });
}

function buildWithTscFallback() {
  run("npm", ["run", "build:ts"]);
  copyFileOrFail(path.join(repoRoot, "src", "frontend.js"), path.join(distDir, "frontend.js"));
  copyFileOrFail(path.join(repoRoot, "src", "optimizer.js"), path.join(distDir, "optimizer.js"));
  copyFileOrFail(path.join(repoRoot, "src", "diagnostics.js"), path.join(distDir, "diagnostics.js"));
}

async function main() {
  fs.rmSync(distDir, { recursive: true, force: true });
  ensureDir(distDir);

  const esbuild = tryLoadEsbuild();
  if (esbuild) {
    await buildWithEsbuild(esbuild);
  } else {
    buildWithTscFallback();
  }

  const mcppDist = path.join(distDir, "mcpp");
  ensureDir(mcppDist);
  copyFileOrFail(path.join(mcppOut, "mcpp_node.js"), path.join(mcppDist, "mcpp_node.js"));
  copyFileOrFail(path.join(mcppOut, "mcpp_core.js"), path.join(mcppDist, "mcpp_core.js"));
  copyFileOrFail(path.join(mcppOut, "mcpp.wasm"), path.join(mcppDist, "mcpp.wasm"));
  copyFileOrFail(path.join(mcppOut, "mcpp_core.wasm"), path.join(mcppDist, "mcpp_core.wasm"));

  console.log("Built VSCode extension bundle in dist/");
}

main().catch((err) => {
  console.error(err && err.stack ? err.stack : String(err));
  process.exit(1);
});
