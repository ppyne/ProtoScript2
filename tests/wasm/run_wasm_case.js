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
  const hostOut = [];
  const hostErr = [];
  let rc = 0;

  const runtime = await ProtoScript({
    print: (text) => out.push(String(text)),
    printErr: (text) => err.push(String(text)),
    noInitialRun: true,
    locateFile: (p, prefix) => path.join(path.dirname(modulePath), p),
  });
  const ttyBytesOut = [];
  const ttyBytesErr = [];
  const outStream = runtime && runtime.FS && runtime.FS.streams ? runtime.FS.streams[1] : null;
  const errStream = runtime && runtime.FS && runtime.FS.streams ? runtime.FS.streams[2] : null;
  let restorePutCharOut = null;
  let restorePutCharErr = null;
  if (outStream && outStream.tty && outStream.tty.ops && typeof outStream.tty.ops.put_char === "function") {
    const orig = outStream.tty.ops.put_char;
    outStream.tty.ops.put_char = (tty, val) => {
      if (typeof val === "number" && val !== -1) ttyBytesOut.push(val & 0xff);
      return orig(tty, val);
    };
    restorePutCharOut = () => { outStream.tty.ops.put_char = orig; };
  }
  if (errStream && errStream.tty && errStream.tty.ops && typeof errStream.tty.ops.put_char === "function") {
    const orig = errStream.tty.ops.put_char;
    errStream.tty.ops.put_char = (tty, val) => {
      if (typeof val === "number" && val !== -1) ttyBytesErr.push(val & 0xff);
      return orig(tty, val);
    };
    restorePutCharErr = () => { errStream.tty.ops.put_char = orig; };
  }

  function ensureDirInMemfs(filePath) {
    const fsPath = filePath.startsWith("/") ? filePath : `/${filePath}`;
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
    return fsPath;
  }

  function memfsCandidatePaths(hostPath) {
    const abs = path.resolve(hostPath);
    const out = [abs.startsWith("/") ? abs : `/${abs}`];
    const cwd = path.resolve(process.cwd());
    if (abs.startsWith(cwd + path.sep) || abs === cwd) {
      const rel = path.relative(cwd, abs);
      out.push(rel.startsWith("/") ? rel : `/${rel}`);
    }
    return [...new Set(out)];
  }

  function copyFileToMemfs(hostFile) {
    const normalizedHost = path.resolve(hostFile);
    const data = fs.readFileSync(normalizedHost, "utf8");
    for (const p of memfsCandidatePaths(normalizedHost)) {
      const memfsPath = ensureDirInMemfs(p);
      runtime.FS.writeFile(memfsPath, data);
    }
    return normalizedHost;
  }

  function copyPathToMemfs(hostPath, seenPaths) {
    const normalized = path.resolve(hostPath);
    if (seenPaths.has(normalized)) return;
    seenPaths.add(normalized);
    if (!fs.existsSync(normalized)) return;
    const st = fs.lstatSync(normalized);
    if (st.isDirectory()) {
      ensureDirInMemfs(normalized);
      for (const ent of fs.readdirSync(normalized)) {
        copyPathToMemfs(path.join(normalized, ent), seenPaths);
      }
      return;
    }
    if (st.isSymbolicLink()) {
      const target = fs.readlinkSync(normalized);
      for (const p of memfsCandidatePaths(normalized)) {
        const memfsPath = ensureDirInMemfs(p);
        try {
          runtime.FS.unlink(memfsPath);
        } catch (_) {}
        runtime.FS.symlink(target, memfsPath);
      }
      return;
    }
    if (st.isFile()) {
      const data = fs.readFileSync(normalized);
      for (const p of memfsCandidatePaths(normalized)) {
        const memfsPath = ensureDirInMemfs(p);
        runtime.FS.writeFile(memfsPath, data);
      }
    }
  }

  function resolveImportPath(fromFile, rawImport) {
    let target = path.resolve(path.dirname(fromFile), rawImport);
    if (fs.existsSync(target) && fs.statSync(target).isFile()) return target;
    if (!target.endsWith(".pts")) {
      const withExt = `${target}.pts`;
      if (fs.existsSync(withExt) && fs.statSync(withExt).isFile()) return withExt;
    }
    return null;
  }

  function collectRelativeImportsRec(entry, seen) {
    const source = fs.readFileSync(entry, "utf8");
    const relImportRegex = /import\s+[^"\n]*"(\.{1,2}\/[^"]+)"/g;
    let m;
    while ((m = relImportRegex.exec(source)) !== null) {
      const rawImport = m[1];
      const dep = resolveImportPath(entry, rawImport);
      if (!dep || seen.has(dep)) continue;
      seen.add(dep);
      collectRelativeImportsRec(dep, seen);
    }
  }

  function copyPtsTree(hostDir, seenDirs) {
    const resolved = path.resolve(hostDir);
    if (seenDirs.has(resolved)) return;
    if (!fs.existsSync(resolved) || !fs.statSync(resolved).isDirectory()) return;
    seenDirs.add(resolved);
    const stack = [resolved];
    while (stack.length > 0) {
      const cur = stack.pop();
      const entries = fs.readdirSync(cur, { withFileTypes: true });
      for (const e of entries) {
        const full = path.join(cur, e.name);
        if (e.isDirectory()) {
          stack.push(full);
        } else if (e.isFile() && full.endsWith(".pts")) {
          copyPathToMemfs(full, seenDirs);
        }
      }
    }
  }

  const srcResolved = path.resolve(srcPath);
  const deps = new Set([srcResolved]);
  collectRelativeImportsRec(srcResolved, deps);
  const copiedPaths = new Set();
  for (const dep of deps) copyPathToMemfs(dep, copiedPaths);
  const copiedRoots = new Set();
  for (const dep of deps) {
    const base = path.dirname(dep);
    copyPtsTree(path.join(base, "modules"), copiedRoots);
    copyPtsTree(path.join(base, "vendor"), copiedRoots);
  }
  copyPathToMemfs(path.resolve("tests/fixtures"), copiedPaths);
  const relFromCwd = path.relative(path.resolve(process.cwd()), srcResolved);
  const fsPath = (!relFromCwd.startsWith("..") && !path.isAbsolute(relFromCwd))
    ? `/${relFromCwd}`
    : (srcResolved.startsWith("/") ? srcResolved : `/${srcResolved}`);
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
  } finally {
    if (restorePutCharOut) restorePutCharOut();
    if (restorePutCharErr) restorePutCharErr();
  }

  // TTY captures raw bytes from the WASM runtime; decode as UTF-8 to preserve
  // exact runtime text (including multi-byte glyphs) for parity comparisons.
  if (ttyBytesOut.length > 0) hostOut.push(Buffer.from(ttyBytesOut).toString("utf8"));
  if (ttyBytesErr.length > 0) hostErr.push(Buffer.from(ttyBytesErr).toString("utf8"));
  const outText = hostOut.length > 0
    ? hostOut.join("")
    : (out.join("\n") + (out.length ? "\n" : ""));
  const errText = hostErr.length > 0
    ? hostErr.join("")
    : (err.join("\n") + (err.length ? "\n" : ""));
  fs.writeFileSync(outPath, outText, "utf8");
  fs.writeFileSync(errPath, errText, "utf8");
  process.exit(rc);
}

main().catch((e) => {
  console.error(e && e.stack ? e.stack : String(e));
  process.exit(1);
});
