"use strict";

const fs = require("fs");
const path = require("path");

async function main() {
  const [, , modulePathArg, srcPathArg, outPathArg, errPathArg] = process.argv;
  if (!modulePathArg || !srcPathArg || !outPathArg || !errPathArg) {
    console.error("usage: node run_wasm_case.js <module.js> <source.pts> <stdout.out> <stderr.out>");
    process.exit(2);
  }

  const modulePath = path.resolve(modulePathArg);
  const srcPath = path.resolve(srcPathArg);
  const outPath = path.resolve(outPathArg);
  const errPath = path.resolve(errPathArg);

  const ProtoScript = require(modulePath);
  const out = [];
  const err = [];
  let rc = 0;

  const runtime = await ProtoScript({
    print: (text) => out.push(String(text)),
    printErr: (text) => err.push(String(text)),
    noInitialRun: true,
    locateFile: (p, prefix) => path.join(path.dirname(modulePath), p),
  });

  const fsPath = srcPath.startsWith("/") ? srcPath : `/${srcPath}`;
  const dir = path.posix.dirname(fsPath);
  const parts = dir.split("/").filter(Boolean);
  let cur = "";
  for (const p of parts) {
    cur += `/${p}`;
    try {
      runtime.FS.mkdir(cur);
    } catch (_) {
      // already exists
    }
  }
  runtime.FS.writeFile(fsPath, fs.readFileSync(srcPath, "utf8"));
  process.exitCode = 0;
  try {
    runtime.callMain(["run", fsPath]);
    rc = typeof process.exitCode === "number" ? process.exitCode : 0;
  } catch (e) {
    if (e && typeof e.status === "number") rc = e.status;
    else {
      rc = typeof process.exitCode === "number" ? process.exitCode : 1;
      err.push(String(e && e.message ? e.message : e));
    }
  }

  fs.writeFileSync(outPath, out.join("\n") + (out.length ? "\n" : ""), "utf8");
  fs.writeFileSync(errPath, err.join("\n") + (err.length ? "\n" : ""), "utf8");
  process.exit(rc);
}

main().catch((e) => {
  console.error(e && e.stack ? e.stack : String(e));
  process.exit(1);
});
