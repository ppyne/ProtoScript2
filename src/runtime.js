"use strict";

const fs = require("fs");
const path = require("path");

const INT64_MIN = -(2n ** 63n);
const INT64_MAX = 2n ** 63n - 1n;

class RuntimeError extends Error {
  constructor(diag) {
    super(diag.message);
    this.diag = diag;
  }
}

function isObjectInstance(v) {
  return v && v.__object === true;
}

function buildPrototypeEnv(ast) {
  const protos = new Map();
  for (const d of ast.decls || []) {
    if (d.kind !== "PrototypeDecl") continue;
    const fields = [];
    for (const f of d.fields || []) fields.push({ name: f.name, type: f.type });
    const methods = new Map();
    for (const m of d.methods || []) methods.set(m.name, m);
    protos.set(d.name, { name: d.name, parent: d.parent || null, fields, methods });
  }
  return protos;
}

function collectPrototypeFields(protos, name) {
  const out = [];
  const chain = [];
  let cur = protos.get(name);
  while (cur) {
    chain.push(cur);
    cur = cur.parent ? protos.get(cur.parent) : null;
  }
  for (let i = chain.length - 1; i >= 0; i -= 1) {
    for (const f of chain[i].fields) out.push(f);
  }
  return out;
}

function defaultValueForTypeNode(protos, t) {
  if (!t || t.kind === "PrimitiveType") {
    const n = t ? t.name : "void";
    if (n === "int") return 0n;
    if (n === "byte") return 0n;
    if (n === "float") return 0.0;
    if (n === "bool") return false;
    if (n === "glyph") return new Glyph(0);
    if (n === "string") return "";
    return null;
  }
  if (t.kind === "NamedType") {
    if (protos.has(t.name)) return clonePrototype(protos, t.name);
    if (t.name === "JSONValue") return makeJsonValue("null", null);
    if (t.name === "File") return null;
    return null;
  }
  if (t.kind === "GenericType") {
    if (t.name === "list") return [];
    if (t.name === "map") return new Map();
    if (t.name === "view" || t.name === "slice") return { __view: true, source: [], offset: 0, len: 0, readonly: t.name === "view" };
  }
  return null;
}

function clonePrototype(protos, name) {
  const fields = collectPrototypeFields(protos, name);
  const obj = { __object: true, __proto: name, __fields: Object.create(null) };
  for (const f of fields) {
    obj.__fields[f.name] = defaultValueForTypeNode(protos, f.type);
  }
  return obj;
}

function resolveProtoMethodRuntime(protos, name, method) {
  let cur = protos.get(name);
  while (cur) {
    if (cur.methods && cur.methods.has(method)) return { proto: cur.name, method: cur.methods.get(method) };
    cur = cur.parent ? protos.get(cur.parent) : null;
  }
  return null;
}

function isExceptionValue(v) {
  return v && v.__exception === true;
}

function makeExceptionValue(opts) {
  const ex = {
    __exception: true,
    type: opts.type || "Exception",
    file: opts.file || "",
    line: Number.isInteger(opts.line) ? opts.line : 1,
    column: Number.isInteger(opts.column) ? opts.column : 1,
    message: typeof opts.message === "string" ? opts.message : "",
    cause: opts.cause || null,
  };
  if (opts.code) ex.code = opts.code;
  if (opts.category) ex.category = opts.category;
  return ex;
}

function setExceptionLocation(ex, file, node) {
  if (!isExceptionValue(ex)) return ex;
  ex.file = file || "";
  ex.line = node && node.line ? node.line : 1;
  ex.column = node && node.col ? node.col : 1;
  return ex;
}

function runtimeErrorToException(err) {
  if (!(err instanceof RuntimeError) || !err.diag) return null;
  const d = err.diag;
  return makeExceptionValue({
    type: "RuntimeException",
    file: d.file || "",
    line: d.line || 1,
    column: d.col || 1,
    message: d.message || "",
    code: d.code,
    category: d.category,
  });
}

function exceptionMatches(ex, typeNode) {
  if (!isExceptionValue(ex)) return false;
  if (!typeNode || typeNode.kind !== "NamedType") return false;
  const t = typeNode.name;
  if (t === "Exception") return true;
  if (t === "RuntimeException") return ex.type === "RuntimeException";
  return false;
}

function valueToString(v) {
  if (isExceptionValue(v)) return v.message || "Exception";
  if (v === null || v === undefined) return "null";
  if (typeof v === "bigint") return v.toString();
  return String(v);
}

class ReturnSignal {
  constructor(value) {
    this.value = value;
  }
}

class Glyph {
  constructor(codepoint) {
    this.value = codepoint >>> 0;
  }
  valueOf() {
    return this.value;
  }
  toString() {
    return String.fromCodePoint(this.value);
  }
}

function makeJsonValue(type, value) {
  return { __json: true, type, value };
}

function isJsonValue(v) {
  return v && v.__json === true;
}

function rdiag(file, node, code, category, message) {
  return {
    file,
    line: node && node.line ? node.line : 1,
    col: node && node.col ? node.col : 1,
    code,
    category,
    message,
  };
}

function isGlyph(v) {
  return v instanceof Glyph;
}

function glyphValue(v) {
  return isGlyph(v) ? v.value : null;
}

function glyphsOf(s) {
  return Array.from(s, (ch) => new Glyph(ch.codePointAt(0)));
}

function glyphStringsOf(s) {
  return Array.from(s);
}

function glyphAt(s, idx) {
  const ch = glyphStringsOf(s)[idx];
  if (ch === undefined) return null;
  return new Glyph(ch.codePointAt(0));
}

function isView(v) {
  return v && v.__view === true;
}

function makeView(file, node, source, offset, len, readonly) {
  const off = Number(offset);
  const ln = Number(len);
  if (!Number.isInteger(off) || !Number.isInteger(ln) || off < 0 || ln < 0) {
    throw new RuntimeError(rdiag(file, node, "R1002", "RUNTIME_INDEX_OOB", "index out of bounds"));
  }
  let base = source;
  let baseOffset = 0;
  if (isView(source)) {
    base = source.source;
    baseOffset = source.offset;
  }
  let totalLen = 0;
  if (Array.isArray(base)) totalLen = base.length;
  else if (typeof base === "string") totalLen = glyphStringsOf(base).length;
  else if (isView(base)) totalLen = base.len;
  if (off + ln > totalLen) {
    throw new RuntimeError(rdiag(file, node, "R1002", "RUNTIME_INDEX_OOB", "index out of bounds"));
  }
  return { __view: true, source: base, offset: baseOffset + off, len: ln, readonly: !!readonly };
}

function trimAscii(s, mode = "both") {
  const chars = glyphStringsOf(s);
  let start = 0;
  let end = chars.length;
  const isWs = (c) => c === " " || c === "\t" || c === "\n" || c === "\r";
  if (mode !== "end") {
    while (start < end && isWs(chars[start])) start += 1;
  }
  if (mode !== "start") {
    while (end > start && isWs(chars[end - 1])) end -= 1;
  }
  return chars.slice(start, end).join("");
}

function indexOfGlyphs(hay, needle) {
  const h = glyphStringsOf(hay);
  const n = glyphStringsOf(needle);
  if (n.length === 0) return 0;
  for (let i = 0; i + n.length <= h.length; i += 1) {
    let ok = true;
    for (let j = 0; j < n.length; j += 1) {
      if (h[i + j] !== n[j]) {
        ok = false;
        break;
      }
    }
    if (ok) return i;
  }
  return -1;
}

function isFloatLiteral(raw) {
  return raw.includes(".") || /[eE]/.test(raw);
}

function parseIntLiteral(raw) {
  if (/^0[xX]/.test(raw)) return BigInt(raw);
  if (/^0[bB]/.test(raw)) return BigInt(raw);
  if (/^0[0-7]+$/.test(raw)) return BigInt(`0o${raw.slice(1)}`);
  return BigInt(raw);
}

function checkIntRange(file, node, n) {
  if (n < INT64_MIN || n > INT64_MAX) {
    throw new RuntimeError(rdiag(file, node, "R1001", "RUNTIME_INT_OVERFLOW", "int overflow"));
  }
  return n;
}

function checkByteRange(file, node, n) {
  if (n < 0n || n > 255n) {
    throw new RuntimeError(rdiag(file, node, "R1008", "RUNTIME_BYTE_RANGE", "byte out of range"));
  }
  return n;
}

const IS_LITTLE_ENDIAN = (() => {
  const buf = new ArrayBuffer(4);
  const view = new DataView(buf);
  view.setUint32(0, 0x01020304, true);
  return new Uint8Array(buf)[0] === 0x04;
})();

function intToBytes(n) {
  const buf = new ArrayBuffer(8);
  const view = new DataView(buf);
  view.setBigInt64(0, BigInt(n), IS_LITTLE_ENDIAN);
  return Array.from(new Uint8Array(buf), (b) => BigInt(b));
}

function floatToBytes(x) {
  const buf = new ArrayBuffer(8);
  const view = new DataView(buf);
  view.setFloat64(0, Number(x), IS_LITTLE_ENDIAN);
  return Array.from(new Uint8Array(buf), (b) => BigInt(b));
}

function parseIntStrict(file, node, s) {
  if (!/^[+-]?(?:0|[1-9][0-9]*)$/.test(s)) {
    throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", "invalid int format"));
  }
  return checkIntRange(file, node, BigInt(s));
}

function parseFloatStrict(file, node, s) {
  if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(s)) {
    throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", "invalid float format"));
  }
  const v = Number(s);
  if (Number.isNaN(v)) {
    throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", "invalid float format"));
  }
  return v;
}

function glyphMatches(re, g) {
  const ch = String.fromCodePoint(g.value);
  return re.test(ch);
}

class Scope {
  constructor(parent = null) {
    this.parent = parent;
    this.values = new Map();
  }

  define(name, value) {
    this.values.set(name, value);
  }

  set(name, value) {
    if (this.values.has(name)) {
      this.values.set(name, value);
      return;
    }
    if (this.parent) {
      this.parent.set(name, value);
      return;
    }
    this.values.set(name, value);
  }

  get(name) {
    if (this.values.has(name)) return this.values.get(name);
    if (this.parent) return this.parent.get(name);
    return undefined;
  }
}

function loadModuleRegistry() {
  const env = process.env.PS_MODULE_REGISTRY;
  const candidates = [];
  if (env) candidates.push(env);
  candidates.push(path.join(process.cwd(), "modules", "registry.json"));
  candidates.push(path.join(__dirname, "..", "modules", "registry.json"));
  for (const c of candidates) {
    if (c && fs.existsSync(c)) {
      try {
        return JSON.parse(fs.readFileSync(c, "utf8"));
      } catch {
        return null;
      }
    }
  }
  return null;
}

class IoFile {
  constructor(fd, flags, isStd = false) {
    this.fd = fd;
    this.flags = flags;
    this.isStd = isStd;
    this.closed = false;
    this.atStart = true;
  }
}

const PS_FILE_READ = 0x01;
const PS_FILE_WRITE = 0x02;
const PS_FILE_APPEND = 0x04;
const PS_FILE_BINARY = 0x08;

const EOF_SENTINEL = { __eof: true };

function decodeUtf8Strict(file, node, bytes, atStart) {
  for (const b of bytes) {
    if (b === 0) throw new RuntimeError(rdiag(file, node, "R1007", "RUNTIME_INVALID_UTF8", "NUL byte not allowed"));
  }
  let start = 0;
  if (atStart && bytes.length >= 3 && bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf) {
    start = 3;
  }
  for (let i = start; i + 2 < bytes.length; i += 1) {
    if (bytes[i] === 0xef && bytes[i + 1] === 0xbb && bytes[i + 2] === 0xbf) {
      throw new RuntimeError(rdiag(file, node, "R1007", "RUNTIME_INVALID_UTF8", "BOM not allowed here"));
    }
  }
  try {
    const dec = new TextDecoder("utf-8", { fatal: true });
    return dec.decode(Uint8Array.from(bytes.slice(start)));
  } catch {
    throw new RuntimeError(rdiag(file, node, "R1007", "RUNTIME_INVALID_UTF8", "invalid UTF-8"));
  }
}

function buildModuleEnv(ast, file) {
  const importedFunctions = new Map();
  const namespaces = new Map();
  const modules = new Map();

  const moduleErrors = new Map([
    ["test.noinit", "module missing ps_module_init"],
    ["test.badver", "module ABI version mismatch"],
    ["test.missing", "module not found"],
  ]);

  const makeModule = (name) => {
    if (!modules.has(name)) {
      modules.set(name, { obj: { __module: name }, constants: new Map(), functions: new Map(), error: moduleErrors.get(name) || null });
    }
    return modules.get(name);
  };

  const toNumberArg = (node, v) => {
    if (typeof v === "bigint") return Number(v);
    if (typeof v === "number") return v;
    throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", "expected float"));
  };

  // JS implementations for tests
  const mathMod = makeModule("Math");
  mathMod.constants.set("PI", Math.PI);
  mathMod.constants.set("E", Math.E);
  mathMod.constants.set("LN2", Math.LN2);
  mathMod.constants.set("LN10", Math.LN10);
  mathMod.constants.set("LOG2E", Math.LOG2E);
  mathMod.constants.set("LOG10E", Math.LOG10E);
  mathMod.constants.set("SQRT1_2", Math.SQRT1_2);
  mathMod.constants.set("SQRT2", Math.SQRT2);
  mathMod.functions.set("abs", (x, node) => Math.abs(toNumberArg(node, x)));
  mathMod.functions.set("min", (a, b, node) => Math.min(toNumberArg(node, a), toNumberArg(node, b)));
  mathMod.functions.set("max", (a, b, node) => Math.max(toNumberArg(node, a), toNumberArg(node, b)));
  mathMod.functions.set("floor", (x, node) => Math.floor(toNumberArg(node, x)));
  mathMod.functions.set("ceil", (x, node) => Math.ceil(toNumberArg(node, x)));
  mathMod.functions.set("round", (x, node) => Math.round(toNumberArg(node, x)));
  mathMod.functions.set("trunc", (x, node) => Math.trunc(toNumberArg(node, x)));
  mathMod.functions.set("sign", (x, node) => Math.sign(toNumberArg(node, x)));
  mathMod.functions.set("fround", (x, node) => Math.fround(toNumberArg(node, x)));
  mathMod.functions.set("sqrt", (x, node) => Math.sqrt(toNumberArg(node, x)));
  mathMod.functions.set("cbrt", (x, node) => Math.cbrt(toNumberArg(node, x)));
  mathMod.functions.set("pow", (a, b, node) => Math.pow(toNumberArg(node, a), toNumberArg(node, b)));
  mathMod.functions.set("sin", (x, node) => Math.sin(toNumberArg(node, x)));
  mathMod.functions.set("cos", (x, node) => Math.cos(toNumberArg(node, x)));
  mathMod.functions.set("tan", (x, node) => Math.tan(toNumberArg(node, x)));
  mathMod.functions.set("asin", (x, node) => Math.asin(toNumberArg(node, x)));
  mathMod.functions.set("acos", (x, node) => Math.acos(toNumberArg(node, x)));
  mathMod.functions.set("atan", (x, node) => Math.atan(toNumberArg(node, x)));
  mathMod.functions.set("atan2", (y, x, node) => Math.atan2(toNumberArg(node, y), toNumberArg(node, x)));
  mathMod.functions.set("sinh", (x, node) => Math.sinh(toNumberArg(node, x)));
  mathMod.functions.set("cosh", (x, node) => Math.cosh(toNumberArg(node, x)));
  mathMod.functions.set("tanh", (x, node) => Math.tanh(toNumberArg(node, x)));
  mathMod.functions.set("asinh", (x, node) => Math.asinh(toNumberArg(node, x)));
  mathMod.functions.set("acosh", (x, node) => Math.acosh(toNumberArg(node, x)));
  mathMod.functions.set("atanh", (x, node) => Math.atanh(toNumberArg(node, x)));
  mathMod.functions.set("log", (x, node) => Math.log(toNumberArg(node, x)));
  mathMod.functions.set("log1p", (x, node) => Math.log1p(toNumberArg(node, x)));
  mathMod.functions.set("log2", (x, node) => Math.log2(toNumberArg(node, x)));
  mathMod.functions.set("log10", (x, node) => Math.log10(toNumberArg(node, x)));
  mathMod.functions.set("exp", (x, node) => Math.exp(toNumberArg(node, x)));
  mathMod.functions.set("expm1", (x, node) => Math.expm1(toNumberArg(node, x)));
  mathMod.functions.set("hypot", (a, b, node) => Math.hypot(toNumberArg(node, a), toNumberArg(node, b)));
  mathMod.functions.set("clz32", (x, node) => Math.clz32(toNumberArg(node, x)));
  mathMod.functions.set("imul", (a, b, node) => Math.imul(toNumberArg(node, a), toNumberArg(node, b)));
  let mathRngState = 0;
  let mathRngSeeded = false;
  const hashString32 = (s) => {
    let h = 2166136261 >>> 0;
    for (let i = 0; i < s.length; i += 1) {
      h ^= s.charCodeAt(i);
      h = Math.imul(h, 16777619) >>> 0;
    }
    return h >>> 0;
  };
  const mathRngSeed = () => {
    const envSeed = Number.parseInt(process.env.PS_RNG_SEED || "", 10);
    let seed = Number.isFinite(envSeed)
      ? envSeed >>> 0
      : hashString32(`${process.cwd()}|${process.execPath}`);
    if (seed === 0) seed = 0x6d2b79f5;
    mathRngState = seed >>> 0;
    mathRngSeeded = true;
  };
  const mathRngNext = () => {
    if (!mathRngSeeded) mathRngSeed();
    let x = mathRngState >>> 0;
    x ^= (x << 13) >>> 0;
    x ^= (x >>> 17) >>> 0;
    x ^= (x << 5) >>> 0;
    mathRngState = x >>> 0;
    return mathRngState;
  };
  mathMod.functions.set("random", () => mathRngNext() / 4294967296);

  const ioMod = makeModule("Io");
  ioMod.constants.set("EOL", "\n");
  ioMod.constants.set("EOF", EOF_SENTINEL);
  ioMod.constants.set("stdin", new IoFile(0, PS_FILE_READ, true));
  ioMod.constants.set("stdout", new IoFile(1, PS_FILE_WRITE, true));
  ioMod.constants.set("stderr", new IoFile(2, PS_FILE_WRITE, true));
  ioMod.functions.set("open", (pathStr, modeStr, node) => {
    if (typeof pathStr !== "string" || typeof modeStr !== "string") {
      throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_IO_ERROR", "Io.open expects (string, string)"));
    }
    const m = modeStr;
    const valid = ["r", "w", "a", "rb", "wb", "ab"];
    if (!valid.includes(m)) {
      throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_IO_ERROR", "invalid mode"));
    }
    const binary = m.includes("b");
    const base = m[0];
    let flags = 0;
    if (base === "r") flags = PS_FILE_READ;
    else if (base === "w") flags = PS_FILE_WRITE;
    else if (base === "a") flags = PS_FILE_APPEND | PS_FILE_WRITE;
    if (binary) flags |= PS_FILE_BINARY;
    try {
      const fd = fs.openSync(pathStr, base);
      return new IoFile(fd, flags, false);
    } catch (e) {
      throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_IO_ERROR", String(e.message || "open failed")));
    }
  });
  ioMod.functions.set("print", (val) => {
    const s = valueToString(val);
    fs.writeSync(1, s);
    return null;
  });
  ioMod.functions.set("printLine", (val) => {
    const s = valueToString(val);
    fs.writeSync(1, s + "\n");
    return null;
  });

  const jsonError = (node, message) => {
    throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_JSON_ERROR", message));
  };

  const jsonEncodeValue = (node, v) => {
    if (isJsonValue(v)) {
      switch (v.type) {
        case "null":
          return "null";
        case "bool":
          return v.value ? "true" : "false";
        case "number":
          if (!Number.isFinite(v.value)) jsonError(node, "invalid JSON number");
          if (Object.is(v.value, -0)) return "-0";
          return String(v.value);
        case "string":
          return JSON.stringify(String(v.value));
        case "array":
          return `[${v.value.map((it) => jsonEncodeValue(node, it)).join(",")}]`;
        case "object": {
          const parts = [];
          for (const [k, val] of v.value.entries()) {
            const key = unmapKey(k);
            if (typeof key !== "string") jsonError(node, "JSON object keys must be strings");
            parts.push(`${JSON.stringify(key)}:${jsonEncodeValue(node, val)}`);
          }
          return `{${parts.join(",")}}`;
        }
        default:
          jsonError(node, "invalid JSON value");
      }
    }
    if (typeof v === "boolean") return v ? "true" : "false";
    if (typeof v === "string") return JSON.stringify(v);
    if (typeof v === "number") {
      if (!Number.isFinite(v)) jsonError(node, "invalid JSON number");
      if (Object.is(v, -0)) return "-0";
      return String(v);
    }
    if (typeof v === "bigint") return v.toString();
    if (Array.isArray(v)) return `[${v.map((it) => jsonEncodeValue(node, it)).join(",")}]`;
    if (v instanceof Map) {
      const parts = [];
      for (const [k, val] of v.entries()) {
        const key = unmapKey(k);
        if (typeof key !== "string") jsonError(node, "JSON object keys must be strings");
        parts.push(`${JSON.stringify(key)}:${jsonEncodeValue(node, val)}`);
      }
      return `{${parts.join(",")}}`;
    }
    jsonError(node, "value not JSON-serializable");
    return "null";
  };

  const jsonDecodeValue = (node, v) => {
    if (v === null) return makeJsonValue("null", null);
    if (typeof v === "boolean") return makeJsonValue("bool", v);
    if (typeof v === "number") {
      if (!Number.isFinite(v)) jsonError(node, "invalid JSON number");
      return makeJsonValue("number", v);
    }
    if (typeof v === "string") return makeJsonValue("string", v);
    if (Array.isArray(v)) return makeJsonValue("array", v.map((it) => jsonDecodeValue(node, it)));
    if (typeof v === "object") {
      const m = new Map();
      for (const [k, val] of Object.entries(v)) {
        m.set(mapKey(k), jsonDecodeValue(node, val));
      }
      return makeJsonValue("object", m);
    }
    jsonError(node, "invalid JSON value");
    return makeJsonValue("null", null);
  };

  const jsonMod = makeModule("JSON");
  jsonMod.functions.set("encode", (v, node) => jsonEncodeValue(node, v));
  jsonMod.functions.set("decode", (s, node) => {
    if (typeof s !== "string") jsonError(node, "decode expects string");
    try {
      const parsed = JSON.parse(s);
      return jsonDecodeValue(node, parsed);
    } catch {
      jsonError(node, "invalid JSON");
    }
  });
  jsonMod.functions.set("isValid", (s, node) => {
    if (typeof s !== "string") jsonError(node, "isValid expects string");
    try {
      JSON.parse(s);
      return true;
    } catch {
      return false;
    }
  });
  jsonMod.functions.set("null", () => makeJsonValue("null", null));
  jsonMod.functions.set("bool", (b, node) => {
    if (typeof b !== "boolean") jsonError(node, "bool expects bool");
    return makeJsonValue("bool", b);
  });
  jsonMod.functions.set("number", (x, node) => {
    const n = typeof x === "bigint" ? Number(x) : x;
    if (typeof n !== "number" || !Number.isFinite(n)) jsonError(node, "number expects finite float");
    return makeJsonValue("number", n);
  });
  jsonMod.functions.set("string", (s, node) => {
    if (typeof s !== "string") jsonError(node, "string expects string");
    return makeJsonValue("string", s);
  });
  jsonMod.functions.set("array", (items, node) => {
    if (!Array.isArray(items)) jsonError(node, "array expects list<JSONValue>");
    for (const it of items) {
      if (!isJsonValue(it)) jsonError(node, "array expects list<JSONValue>");
    }
    return makeJsonValue("array", items);
  });
  jsonMod.functions.set("object", (members, node) => {
    if (!(members instanceof Map)) jsonError(node, "object expects map<string,JSONValue>");
    for (const [k, v] of members.entries()) {
      const key = unmapKey(k);
      if (typeof key !== "string") jsonError(node, "object expects map<string,JSONValue>");
      if (!isJsonValue(v)) jsonError(node, "object expects map<string,JSONValue>");
    }
    return makeJsonValue("object", members);
  });

  const simpleMod = makeModule("test.simple");
  simpleMod.functions.set("add", (a, b) => BigInt(a) + BigInt(b));

  const utf8Mod = makeModule("test.utf8");
  utf8Mod.functions.set("roundtrip", (s) => {
    if (typeof s !== "string") throw new RuntimeError(rdiag(file, null, "R1010", "RUNTIME_IO_ERROR", "expected string"));
    return s;
  });

  const throwMod = makeModule("test.throw");
  throwMod.functions.set("fail", () => {
    throw new RuntimeError(rdiag(file, null, "R1010", "RUNTIME_MODULE_ERROR", "native throw"));
  });

  makeModule("test.nosym");
  loadModuleRegistry(); // ensures registry exists if needed

  const imports = ast.imports || [];
  for (const imp of imports) {
    const modName = imp.modulePath.join(".");
    if (!imp.items || imp.items.length === 0) {
      const alias = imp.alias || imp.modulePath[imp.modulePath.length - 1];
      namespaces.set(alias, modName);
      makeModule(modName);
    } else {
      for (const it of imp.items) {
        const local = it.alias || it.name;
        importedFunctions.set(local, { module: modName, name: it.name, node: it });
      }
    }
  }

  return { modules, namespaces, importedFunctions };
}

function runProgram(ast, file, argv) {
  const functions = new Map();
  const protoEnv = buildPrototypeEnv(ast);
  for (const d of ast.decls) {
    if (d.kind === "FunctionDecl") functions.set(d.name, d);
    if (d.kind === "PrototypeDecl") {
      for (const m of d.methods || []) {
        m.__protoOwner = d.name;
        functions.set(`${d.name}.${m.name}`, m);
      }
    }
  }
  const main = functions.get("main");
  if (!main) return;

  const moduleEnv = buildModuleEnv(ast, file);
  const globalScope = new Scope(null);
  for (const [alias, modName] of moduleEnv.namespaces.entries()) {
    const mod = moduleEnv.modules.get(modName);
    if (mod) globalScope.define(alias, mod.obj);
  }

  const callFunction = (fn, args) => {
    const scope = new Scope(globalScope);
    let argIndex = 0;
    if (fn.__protoOwner) {
      scope.define("self", args[0]);
      argIndex = 1;
    }
    for (let i = 0; i < fn.params.length; i += 1) {
      scope.define(fn.params[i].name, args[argIndex + i]);
    }
    try {
      execBlock(fn.body, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return null;
    } catch (e) {
      if (e instanceof ReturnSignal) return e.value;
      throw e;
    }
  };

  const argList = Array.isArray(argv) ? argv.slice() : [];
  const mainArgs = main.params && main.params.length === 1 ? [argList] : [];
  callFunction(main, mainArgs);
}

function execBlock(block, scope, functions, moduleEnv, protoEnv, file, callFunction) {
  const local = new Scope(scope);
  for (const stmt of block.stmts) execStmt(stmt, local, functions, moduleEnv, protoEnv, file, callFunction);
}

function execStmt(stmt, scope, functions, moduleEnv, protoEnv, file, callFunction) {
  switch (stmt.kind) {
    case "VarDecl": {
      let v = null;
      if (stmt.init) v = evalExpr(stmt.init, scope, functions, moduleEnv, protoEnv, file, callFunction);
      scope.define(stmt.name, v);
      return;
    }
    case "AssignStmt": {
      const rhs = evalExpr(stmt.expr, scope, functions, moduleEnv, protoEnv, file, callFunction);
      if (stmt.target.kind === "Identifier") {
        scope.set(stmt.target.name, rhs);
      } else if (stmt.target.kind === "MemberExpr") {
        const target = evalExpr(stmt.target.target, scope, functions, moduleEnv, protoEnv, file, callFunction);
        if (!isObjectInstance(target)) {
          throw new RuntimeError(rdiag(file, stmt.target, "R1010", "RUNTIME_TYPE_ERROR", "member assignment on non-object"));
        }
        if (!(stmt.target.name in target.__fields)) {
          throw new RuntimeError(rdiag(file, stmt.target, "R1010", "RUNTIME_TYPE_ERROR", "unknown field"));
        }
        target.__fields[stmt.target.name] = rhs;
      } else if (stmt.target.kind === "IndexExpr") {
        const target = evalExpr(stmt.target.target, scope, functions, moduleEnv, protoEnv, file, callFunction);
        const idx = evalExpr(stmt.target.index, scope, functions, moduleEnv, protoEnv, file, callFunction);
        assignIndex(file, stmt.target.target, stmt.target.index, target, idx, rhs);
      }
      return;
    }
    case "ExprStmt":
      evalExpr(stmt.expr, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return;
    case "ReturnStmt": {
      const v = stmt.expr ? evalExpr(stmt.expr, scope, functions, moduleEnv, protoEnv, file, callFunction) : null;
      throw new ReturnSignal(v);
    }
    case "ThrowStmt": {
      const v = stmt.expr ? evalExpr(stmt.expr, scope, functions, moduleEnv, protoEnv, file, callFunction) : null;
      if (!isExceptionValue(v)) {
        throw new RuntimeError(rdiag(file, stmt, "R1010", "RUNTIME_TYPE_ERROR", "throw expects Exception"));
      }
      setExceptionLocation(v, file, stmt);
      throw v;
    }
    case "Block":
      execBlock(stmt, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return;
    case "ForStmt":
      execFor(stmt, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return;
    case "SwitchStmt":
      execSwitch(stmt, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return;
    case "TryStmt": {
      const hasCatch = stmt.catches && stmt.catches.length > 0;
      let pending = null;
      try {
        execBlock(stmt.tryBlock, scope, functions, moduleEnv, protoEnv, file, callFunction);
      } catch (e) {
        if (e instanceof ReturnSignal) throw e;
        let ex = null;
        if (e instanceof RuntimeError) ex = runtimeErrorToException(e);
        else if (isExceptionValue(e)) ex = e;
        if (!hasCatch || !ex) {
          pending = e;
        } else {
          let handled = false;
          for (const c of stmt.catches) {
            if (exceptionMatches(ex, c.type)) {
              const cs = new Scope(scope);
              cs.define(c.name, ex);
              execBlock(c.block, cs, functions, moduleEnv, protoEnv, file, callFunction);
              handled = true;
              break;
            }
          }
          if (!handled) pending = ex;
        }
      } finally {
        if (stmt.finallyBlock) execBlock(stmt.finallyBlock, scope, functions, moduleEnv, protoEnv, file, callFunction);
      }
      if (pending) throw pending;
      return;
    }
    case "BreakStmt":
    case "ContinueStmt":
      return;
    default:
      return;
  }
}

function execFor(stmt, scope, functions, moduleEnv, protoEnv, file, callFunction) {
  if (stmt.forKind === "classic") {
    const s = new Scope(scope);
    if (stmt.init) {
      if (stmt.init.kind === "VarDecl" || stmt.init.kind === "AssignStmt") execStmt(stmt.init, s, functions, moduleEnv, protoEnv, file, callFunction);
      else evalExpr(stmt.init, s, functions, moduleEnv, protoEnv, file, callFunction);
    }
    while (true) {
      if (stmt.cond) {
        const c = evalExpr(stmt.cond, s, functions, moduleEnv, protoEnv, file, callFunction);
        if (!Boolean(c)) break;
      }
      execStmt(stmt.body, s, functions, moduleEnv, protoEnv, file, callFunction);
      if (stmt.step) {
        if (stmt.step.kind === "VarDecl" || stmt.step.kind === "AssignStmt") execStmt(stmt.step, s, functions, moduleEnv, protoEnv, file, callFunction);
        else evalExpr(stmt.step, s, functions, moduleEnv, protoEnv, file, callFunction);
      }
    }
    return;
  }

  const seq = evalExpr(stmt.iterExpr, scope, functions, moduleEnv, protoEnv, file, callFunction);
  const s = new Scope(scope);
  let items = null;
  if (Array.isArray(seq)) {
    items = seq;
  } else if (isView(seq)) {
    items = [];
    for (let i = 0; i < seq.len; i += 1) {
      if (Array.isArray(seq.source)) items.push(seq.source[seq.offset + i]);
      else if (typeof seq.source === "string") items.push(glyphAt(seq.source, seq.offset + i));
    }
  } else if (typeof seq === "string") {
    items = glyphsOf(seq);
  } else if (seq instanceof Map) {
    if (stmt.forKind === "in") {
      items = Array.from(seq.keys()).map(unmapKey);
    } else {
      items = Array.from(seq.values());
    }
  } else {
    return;
  }
  for (const item of items) {
    if (stmt.iterVar) s.define(stmt.iterVar.name, item);
    execStmt(stmt.body, s, functions, moduleEnv, protoEnv, file, callFunction);
  }
}

function execSwitch(stmt, scope, functions, moduleEnv, protoEnv, file, callFunction) {
  const v = evalExpr(stmt.expr, scope, functions, moduleEnv, protoEnv, file, callFunction);
  for (const c of stmt.cases) {
    const cv = evalExpr(c.value, scope, functions, moduleEnv, protoEnv, file, callFunction);
    if (eqValue(v, cv)) {
      for (const st of c.stmts) execStmt(st, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return;
    }
  }
  if (stmt.defaultCase) {
    for (const st of stmt.defaultCase.stmts) execStmt(st, scope, functions, moduleEnv, protoEnv, file, callFunction);
  }
}

function eqValue(a, b) {
  if (isGlyph(a) && isGlyph(b)) return a.value === b.value;
  if (typeof a === "bigint" || typeof b === "bigint") return BigInt(a) === BigInt(b);
  return a === b;
}

function evalExpr(expr, scope, functions, moduleEnv, protoEnv, file, callFunction) {
  switch (expr.kind) {
    case "Literal":
      if (expr.literalType === "int") return parseIntLiteral(expr.value);
      if (expr.literalType === "float") return Number(expr.value);
      if (expr.literalType === "bool") return Boolean(expr.value);
      if (expr.literalType === "string") return String(expr.value);
      return expr.value;
    case "Identifier":
      return scope.get(expr.name);
    case "UnaryExpr": {
      const v = evalExpr(expr.expr, scope, functions, moduleEnv, protoEnv, file, callFunction);
      if (isGlyph(v) && (expr.op === "-" || expr.op === "++" || expr.op === "--" || expr.op === "~")) {
        throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "invalid glyph operation"));
      }
      if (expr.op === "-") {
        if (typeof v === "bigint") return checkIntRange(file, expr.expr, -v);
        return -v;
      }
      if (expr.op === "!") return !Boolean(v);
      if (expr.op === "~") return ~BigInt(v);
      if (expr.op === "++" || expr.op === "--") {
        const d = expr.op === "++" ? 1n : -1n;
        return checkIntRange(file, expr.expr, BigInt(v) + d);
      }
      return v;
    }
    case "BinaryExpr": {
      const l = evalExpr(expr.left, scope, functions, moduleEnv, protoEnv, file, callFunction);
      const r = evalExpr(expr.right, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return evalBinary(file, expr, l, r);
    }
    case "ConditionalExpr": {
      const c = evalExpr(expr.cond, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return c
        ? evalExpr(expr.thenExpr, scope, functions, moduleEnv, protoEnv, file, callFunction)
        : evalExpr(expr.elseExpr, scope, functions, moduleEnv, protoEnv, file, callFunction);
    }
    case "CallExpr":
      return evalCall(expr, scope, functions, moduleEnv, protoEnv, file, callFunction);
    case "IndexExpr": {
      const target = evalExpr(expr.target, scope, functions, moduleEnv, protoEnv, file, callFunction);
      const idx = evalExpr(expr.index, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return indexGet(file, expr.target, expr.index, target, idx);
    }
    case "MemberExpr": {
      const target = evalExpr(expr.target, scope, functions, moduleEnv, protoEnv, file, callFunction);
      if (target && target.__module && moduleEnv) {
        const mod = moduleEnv.modules.get(target.__module);
        if (mod && mod.constants.has(expr.name)) return mod.constants.get(expr.name);
      }
      if (isExceptionValue(target)) {
        if (expr.name === "file") return target.file || "";
        if (expr.name === "line") return target.line || 1;
        if (expr.name === "column") return target.column || 1;
        if (expr.name === "message") return target.message || "";
        if (expr.name === "cause") return target.cause || null;
        if (expr.name === "code") return target.code || "";
        if (expr.name === "category") return target.category || "";
        return null;
      }
      if (isObjectInstance(target)) {
        if (!(expr.name in target.__fields)) {
          throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "unknown field"));
        }
        return target.__fields[expr.name];
      }
      return { __member__: true, target, name: expr.name, node: expr };
    }
    case "PostfixExpr": {
      const v = evalExpr(expr.expr, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return v;
    }
    case "ListLiteral":
      return expr.items.map((it) => evalExpr(it, scope, functions, moduleEnv, protoEnv, file, callFunction));
    case "MapLiteral": {
      const m = new Map();
      for (const p of expr.pairs) {
        const k = evalExpr(p.key, scope, functions, moduleEnv, protoEnv, file, callFunction);
        const v = evalExpr(p.value, scope, functions, moduleEnv, protoEnv, file, callFunction);
        m.set(mapKey(k), v);
      }
      return m;
    }
    default:
      return null;
  }
}

function mapKey(v) {
  if (isGlyph(v)) return `g:${v.value}`;
  if (typeof v === "bigint") return `i:${v.toString()}`;
  return `${typeof v}:${String(v)}`;
}

function unmapKey(k) {
  if (k.startsWith("i:")) return BigInt(k.slice(2));
  if (k.startsWith("g:")) return new Glyph(Number(k.slice(2)));
  const idx = k.indexOf(":");
  if (idx < 0) return k;
  const type = k.slice(0, idx);
  const raw = k.slice(idx + 1);
  if (type === "string") return raw;
  if (type === "boolean") return raw === "true";
  if (type === "number") return Number(raw);
  return raw;
}

function evalBinary(file, expr, l, r) {
  const op = expr.op;
  if (isGlyph(l) || isGlyph(r)) {
    if (!isGlyph(l) || !isGlyph(r)) {
      throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "invalid glyph operation"));
    }
    if (op === "==") return l.value === r.value;
    if (op === "!=") return l.value !== r.value;
    if (op === "<") return l.value < r.value;
    if (op === "<=") return l.value <= r.value;
    if (op === ">") return l.value > r.value;
    if (op === ">=") return l.value >= r.value;
    throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "invalid glyph operation"));
  }
  if (typeof l === "bigint" && typeof r === "bigint") {
    if (op === "+") return checkIntRange(file, expr.left, l + r);
    if (op === "-") return checkIntRange(file, expr.left, l - r);
    if (op === "*") return checkIntRange(file, expr.left, l * r);
    if (op === "/") {
      if (r === 0n) throw new RuntimeError(rdiag(file, expr.right, "R1004", "RUNTIME_DIVIDE_BY_ZERO", "division by zero"));
      return l / r;
    }
    if (op === "%") {
      if (r === 0n) throw new RuntimeError(rdiag(file, expr.right, "R1004", "RUNTIME_DIVIDE_BY_ZERO", "division by zero"));
      return l % r;
    }
    if (op === "<<") {
      if (r < 0n || r >= 64n) throw new RuntimeError(rdiag(file, expr.right, "R1005", "RUNTIME_SHIFT_RANGE", "invalid shift"));
      return checkIntRange(file, expr.left, l << r);
    }
    if (op === ">>") {
      if (r < 0n || r >= 64n) throw new RuntimeError(rdiag(file, expr.right, "R1005", "RUNTIME_SHIFT_RANGE", "invalid shift"));
      return l >> r;
    }
    if (op === "&") return l & r;
    if (op === "|") return l | r;
    if (op === "^") return l ^ r;
    if (op === "==") return l === r;
    if (op === "!=") return l !== r;
    if (op === "<") return l < r;
    if (op === "<=") return l <= r;
    if (op === ">") return l > r;
    if (op === ">=") return l >= r;
  }

  if (op === "&&") return Boolean(l) && Boolean(r);
  if (op === "||") return Boolean(l) || Boolean(r);
  if (op === "==") return l === r;
  if (op === "!=") return l !== r;
  if (op === "<") return l < r;
  if (op === "<=") return l <= r;
  if (op === ">") return l > r;
  if (op === ">=") return l >= r;
  if (op === "+") return l + r;
  if (op === "-") return l - r;
  if (op === "*") return l * r;
  if (op === "/") return l / r;
  if (op === "%") return l % r;
  return null;
}

function indexGet(file, targetNode, indexNode, target, idx) {
  if (Array.isArray(target)) {
    const i = Number(idx);
    if (!Number.isInteger(i) || i < 0 || i >= target.length) {
      throw new RuntimeError(rdiag(file, targetNode, "R1002", "RUNTIME_INDEX_OOB", "index out of bounds"));
    }
    return target[i];
  }
  if (isView(target)) {
    const i = Number(idx);
    if (!Number.isInteger(i) || i < 0 || i >= target.len) {
      throw new RuntimeError(rdiag(file, targetNode, "R1002", "RUNTIME_INDEX_OOB", "index out of bounds"));
    }
    if (Array.isArray(target.source)) return target.source[target.offset + i];
    if (typeof target.source === "string") return glyphAt(target.source, target.offset + i);
    return null;
  }
  if (typeof target === "string") {
    const glyphs = glyphStringsOf(target);
    const i = Number(idx);
    if (!Number.isInteger(i) || i < 0 || i >= glyphs.length) {
      throw new RuntimeError(rdiag(file, targetNode, "R1002", "RUNTIME_INDEX_OOB", "index out of bounds"));
    }
    return glyphAt(target, i);
  }
  if (target instanceof Map) {
    const k = mapKey(idx);
    if (!target.has(k)) {
      throw new RuntimeError(rdiag(file, targetNode, "R1003", "RUNTIME_MISSING_KEY", "missing key"));
    }
    return target.get(k);
  }
  return null;
}

function assignIndex(file, targetNode, indexNode, target, idx, rhs) {
  if (Array.isArray(target)) {
    const i = Number(idx);
    if (!Number.isInteger(i) || i < 0 || i >= target.length) {
      throw new RuntimeError(rdiag(file, targetNode, "R1002", "RUNTIME_INDEX_OOB", "index out of bounds"));
    }
    target[i] = rhs;
    return;
  }
  if (isView(target)) {
    if (target.readonly) {
      throw new RuntimeError(rdiag(file, targetNode, "R1010", "RUNTIME_TYPE_ERROR", "cannot assign through view"));
    }
    const i = Number(idx);
    if (!Number.isInteger(i) || i < 0 || i >= target.len) {
      throw new RuntimeError(rdiag(file, targetNode, "R1002", "RUNTIME_INDEX_OOB", "index out of bounds"));
    }
    if (Array.isArray(target.source)) {
      target.source[target.offset + i] = rhs;
      return;
    }
    throw new RuntimeError(rdiag(file, targetNode, "R1010", "RUNTIME_TYPE_ERROR", "invalid slice target"));
  }
  if (target instanceof Map) {
    target.set(mapKey(idx), rhs);
  }
}

function evalCall(expr, scope, functions, moduleEnv, protoEnv, file, callFunction) {
  // Function call by identifier.
  if (expr.callee.kind === "Identifier") {
    const fn = functions.get(expr.callee.name);
    if (!fn && expr.callee.name === "Exception") {
      const args = expr.args.map((a) => evalExpr(a, scope, functions, moduleEnv, protoEnv, file, callFunction));
      if (args.length > 2) {
        throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "Exception expects (string message, Exception cause?)"));
      }
      if (args.length >= 1 && typeof args[0] !== "string") {
        throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "Exception message must be string"));
      }
      if (args.length === 2 && args[1] !== null && !isExceptionValue(args[1])) {
        throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "Exception cause must be Exception"));
      }
      return makeExceptionValue({
        type: "Exception",
        file: "",
        line: 1,
        column: 1,
        message: args.length >= 1 ? args[0] : "",
        cause: args.length === 2 ? args[1] : null,
      });
    }
    if (!fn && expr.callee.name === "RuntimeException") {
      throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "RuntimeException is not constructible"));
    }
    if (!fn && moduleEnv && moduleEnv.importedFunctions.has(expr.callee.name)) {
      const info = moduleEnv.importedFunctions.get(expr.callee.name);
      const mod = moduleEnv.modules.get(info.module);
      if (!mod) {
        throw new RuntimeError(rdiag(file, info.node, "R1010", "RUNTIME_MODULE_ERROR", "module not found"));
      }
      if (mod.error) {
        throw new RuntimeError(rdiag(file, info.node, "R1010", "RUNTIME_MODULE_ERROR", mod.error));
      }
      const impl = mod.functions.get(info.name);
      if (!impl) {
        throw new RuntimeError(rdiag(file, info.node, "R1010", "RUNTIME_MODULE_ERROR", "symbol not found"));
      }
      const args = expr.args.map((a) => evalExpr(a, scope, functions, moduleEnv, protoEnv, file, callFunction));
      return impl(...args, info.node);
    }
    if (!fn) return null;
    const args = expr.args.map((a) => evalExpr(a, scope, functions, moduleEnv, protoEnv, file, callFunction));
    return callFunction(fn, args);
  }

  // Member call.
  if (expr.callee.kind === "MemberExpr") {
    const m = expr.callee;
    const target = evalExpr(m.target, scope, functions, moduleEnv, protoEnv, file, callFunction);
    const args = expr.args.map((a) => evalExpr(a, scope, functions, moduleEnv, protoEnv, file, callFunction));

    if (protoEnv && (expr._protoClone || (m.target.kind === "Identifier" && protoEnv.has(m.target.name) && m.name === "clone"))) {
      const pname = expr._protoClone || m.target.name;
      if (!protoEnv.has(pname)) {
        throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "unknown prototype"));
      }
      return clonePrototype(protoEnv, pname);
    }
    if (protoEnv && expr._protoStatic) {
      const info = resolveProtoMethodRuntime(protoEnv, expr._protoStatic, m.name);
      if (!info) throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "unknown prototype method"));
      const fn = functions.get(`${info.proto}.${m.name}`);
      if (!fn) throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "missing method"));
      return callFunction(fn, args);
    }
    if (protoEnv && expr._protoInstance) {
      const info = resolveProtoMethodRuntime(protoEnv, expr._protoInstance, m.name);
      if (!info) throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "unknown prototype method"));
      const fn = functions.get(`${info.proto}.${m.name}`);
      if (!fn) throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "missing method"));
      return callFunction(fn, [target, ...args]);
    }

    if (isJsonValue(target)) {
      const t = target.type;
      if (m.name === "isNull") return t === "null";
      if (m.name === "isBool") return t === "bool";
      if (m.name === "isNumber") return t === "number";
      if (m.name === "isString") return t === "string";
      if (m.name === "isArray") return t === "array";
      if (m.name === "isObject") return t === "object";
      if (m.name === "asBool") {
        if (t !== "bool") throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_JSON_ERROR", "expected JsonBool"));
        return target.value;
      }
      if (m.name === "asNumber") {
        if (t !== "number") throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_JSON_ERROR", "expected JsonNumber"));
        return target.value;
      }
      if (m.name === "asString") {
        if (t !== "string") throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_JSON_ERROR", "expected JsonString"));
        return target.value;
      }
      if (m.name === "asArray") {
        if (t !== "array") throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_JSON_ERROR", "expected JsonArray"));
        return target.value;
      }
      if (m.name === "asObject") {
        if (t !== "object") throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_JSON_ERROR", "expected JsonObject"));
        return target.value;
      }
    }

    if (m.name === "toString") {
      if (target === null || target === undefined) return "null";
      if (isExceptionValue(target)) return target.message || "Exception";
      if (isGlyph(target)) return String.fromCodePoint(target.value);
      if (typeof target === "bigint") return target.toString();
      if (typeof target === "string") return target;
      if (typeof target === "boolean") return target ? "true" : "false";
      return String(target);
    }
    if (isGlyph(target)) {
      if (m.name === "isLetter") return glyphMatches(/\p{L}/u, target);
      if (m.name === "isDigit") return glyphMatches(/\p{Nd}/u, target);
      if (m.name === "isWhitespace") return glyphMatches(/\p{White_Space}/u, target);
      if (m.name === "isUpper") return glyphMatches(/\p{Lu}/u, target);
      if (m.name === "isLower") return glyphMatches(/\p{Ll}/u, target);
      if (m.name === "toUpper") {
        const up = String.fromCodePoint(target.value).toUpperCase();
        const ch = Array.from(up)[0] || String.fromCodePoint(target.value);
        return new Glyph(ch.codePointAt(0));
      }
      if (m.name === "toLower") {
        const lo = String.fromCodePoint(target.value).toLowerCase();
        const ch = Array.from(lo)[0] || String.fromCodePoint(target.value);
        return new Glyph(ch.codePointAt(0));
      }
      if (m.name === "toInt") return BigInt(target.value);
      if (m.name === "toUtf8Bytes") {
        const enc = new TextEncoder();
        const bytes = enc.encode(String.fromCodePoint(target.value));
        return Array.from(bytes, (b) => BigInt(b));
      }
    }
    if (m.name === "toByte") {
      if (typeof target === "bigint") return checkByteRange(file, m, target);
      if (typeof target === "number") return checkByteRange(file, m, BigInt(Math.trunc(target)));
      throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "toByte expects int"));
    }
    if (m.name === "toInt") {
      if (typeof target === "bigint") return target;
      if (typeof target === "number") {
        if (!Number.isFinite(target)) throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "invalid float to int"));
        return checkIntRange(file, m, BigInt(Math.trunc(target)));
      }
      if (typeof target === "string") return parseIntStrict(file, m, target);
    }
    if (m.name === "toFloat") {
      if (typeof target === "bigint") return Number(target);
      if (typeof target === "number") return target;
      if (typeof target === "string") return parseFloatStrict(file, m, target);
    }
    if (m.name === "toBytes") {
      if (typeof target === "bigint") return intToBytes(target);
      if (typeof target === "number") return floatToBytes(target);
    }
    if (typeof target === "bigint") {
      if (m.name === "abs") return checkIntRange(file, m, target < 0n ? -target : target);
      if (m.name === "sign") return target === 0n ? 0n : target > 0n ? 1n : -1n;
    }
    if (typeof target === "number") {
      if (m.name === "abs") return Math.abs(target);
      if (m.name === "isNaN") return Number.isNaN(target);
      if (m.name === "isInfinite") return !Number.isFinite(target);
      if (m.name === "isFinite") return Number.isFinite(target);
    }

    if (m.name === "view" || m.name === "slice") {
      if (typeof target === "string" && m.name === "view") {
        if (args.length === 0) {
          const len = glyphStringsOf(target).length;
          return makeView(file, m, target, 0, len, true);
        }
        if (args.length === 2) {
          return makeView(file, m, target, args[0], args[1], true);
        }
        throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "invalid view arity"));
      }
      if (Array.isArray(target)) {
        if (args.length !== 2) throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "invalid view/slice arity"));
        return makeView(file, m, target, args[0], args[1], m.name === "view");
      }
      if (isView(target)) {
        if (args.length !== 2) throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "invalid view/slice arity"));
        if (m.name === "view" && target.readonly) return makeView(file, m, target, args[0], args[1], true);
        if (m.name === "slice" && !target.readonly) return makeView(file, m, target, args[0], args[1], false);
        throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "invalid view/slice target"));
      }
    }

    if (m.name === "length") {
      if (Array.isArray(target)) return BigInt(target.length);
      if (typeof target === "string") return BigInt(glyphStringsOf(target).length);
      if (target instanceof Map) return BigInt(target.size);
      if (isView(target)) return BigInt(target.len);
      return 0n;
    }
    if (m.name === "isEmpty") {
      if (Array.isArray(target)) return target.length === 0;
      if (typeof target === "string") return glyphStringsOf(target).length === 0;
      if (target instanceof Map) return target.size === 0;
      if (isView(target)) return target.len === 0;
      return false;
    }
    if (target instanceof Map) {
      if (m.name === "containsKey") {
        if (target.size > 0) {
          const firstKey = target.keys().next().value;
          const expected = String(firstKey).split(":", 1)[0];
          const actual = mapKey(args[0]).split(":", 1)[0];
          if (expected !== actual) {
            throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "map key type mismatch"));
          }
        }
        return target.has(mapKey(args[0]));
      }
      if (m.name === "keys") return Array.from(target.keys()).map(unmapKey);
      if (m.name === "values") return Array.from(target.values());
    }

    if (typeof target === "string") {
      if (m.name === "toUpper") return target.toUpperCase();
      if (m.name === "toLower") return target.toLowerCase();
      if (m.name === "concat") return String(target) + String(args[0] ?? "");
      if (m.name === "substring") {
        const start = Number(args[0]);
        const length = Number(args[1]);
        const gs = glyphStringsOf(target);
        if (!Number.isInteger(start) || !Number.isInteger(length) || start < 0 || length < 0 || start + length > gs.length) {
          throw new RuntimeError(rdiag(file, m, "R1002", "RUNTIME_INDEX_OOB", "index out of bounds"));
        }
        return gs.slice(start, start + length).join("");
      }
      if (m.name === "indexOf") {
        const needle = String(args[0] ?? "");
        return BigInt(indexOfGlyphs(target, needle));
      }
      if (m.name === "startsWith") return target.startsWith(String(args[0] ?? ""));
      if (m.name === "endsWith") return target.endsWith(String(args[0] ?? ""));
      if (m.name === "split") {
        const sep = String(args[0] ?? "");
        if (sep === "") return glyphStringsOf(target);
        return target.split(sep);
      }
      if (m.name === "trim") return trimAscii(target, "both");
      if (m.name === "trimStart") return trimAscii(target, "start");
      if (m.name === "trimEnd") return trimAscii(target, "end");
      if (m.name === "replace") return target.replace(String(args[0] ?? ""), String(args[1] ?? ""));
      if (m.name === "toUtf8Bytes") {
        const enc = new TextEncoder();
        const bytes = enc.encode(target);
        return Array.from(bytes, (b) => BigInt(b));
      }
    }

    if (Array.isArray(target) && m.name === "toUtf8String") {
      const bytes = [];
      for (const v of target) {
        const n = typeof v === "bigint" ? Number(v) : Number(v);
        if (!Number.isInteger(n) || n < 0 || n > 255) {
          throw new RuntimeError(rdiag(file, m, "R1007", "RUNTIME_INVALID_UTF8", "invalid UTF-8"));
        }
        bytes.push(n);
      }
      try {
        const dec = new TextDecoder("utf-8", { fatal: true });
        return dec.decode(Uint8Array.from(bytes));
      } catch {
        throw new RuntimeError(rdiag(file, m, "R1007", "RUNTIME_INVALID_UTF8", "invalid UTF-8"));
      }
    }
    if (Array.isArray(target)) {
      if (m.name === "push") {
        target.push(args[0]);
        return BigInt(target.length);
      }
      if (m.name === "contains") {
        for (const v of target) {
          if (eqValue(v, args[0])) return true;
        }
        return false;
      }
      if (m.name === "sort") {
        if (target.length === 0) return BigInt(0);
        const first = target[0];
        let kind = "unknown";
        if (typeof first === "bigint") kind = "int";
        else if (typeof first === "number") kind = "float";
        else if (typeof first === "string") kind = "string";
        else if (isGlyph(first)) kind = "glyph";
        else if (typeof first === "boolean") kind = "bool";
        if (kind === "unknown") {
          throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "list element not comparable"));
        }
        for (const v of target) {
          if (
            (kind === "int" && typeof v !== "bigint") ||
            (kind === "float" && typeof v !== "number") ||
            (kind === "string" && typeof v !== "string") ||
            (kind === "glyph" && !isGlyph(v)) ||
            (kind === "bool" && typeof v !== "boolean")
          ) {
            throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "list element not comparable"));
          }
        }
        const cmp = (a, b) => {
          if (kind === "int") return a < b ? -1 : a > b ? 1 : 0;
          if (kind === "float") return a < b ? -1 : a > b ? 1 : 0;
          if (kind === "string") return a < b ? -1 : a > b ? 1 : 0;
          if (kind === "glyph") return a.value < b.value ? -1 : a.value > b.value ? 1 : 0;
          if (kind === "bool") return a === b ? 0 : a ? 1 : -1;
          return 0;
        };
        if (target.length > 1) target.sort(cmp);
        return BigInt(target.length);
      }
    }
    if (Array.isArray(target) && (m.name === "join" || m.name === "concat")) {
      for (const v of target) {
        if (typeof v !== "string") {
          throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "join expects list<string>"));
        }
      }
      if (m.name === "concat") return target.join("");
      const sep = args.length > 0 ? args[0] : "";
      if (typeof sep !== "string") {
        throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "join separator must be string"));
      }
      return target.join(sep);
    }

    if (target instanceof IoFile) {
      if (m.name === "close") {
        if (target.isStd) {
          throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_IO_ERROR", "cannot close standard stream"));
        }
        if (!target.closed) {
          fs.closeSync(target.fd);
          target.closed = true;
        }
        return null;
      }
      if (target.closed) {
        throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_IO_ERROR", "file is closed"));
      }
      const isBinary = (target.flags & PS_FILE_BINARY) !== 0;
      const canRead = (target.flags & PS_FILE_READ) !== 0;
      const canWrite = (target.flags & (PS_FILE_WRITE | PS_FILE_APPEND)) !== 0;
      if (m.name === "read") {
        if (!canRead) throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_IO_ERROR", "file not readable"));
        const hasSize = args.length > 0;
        if (hasSize) {
          const size = Number(args[0]);
          if (!Number.isInteger(size) || size <= 0) {
            throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_IO_ERROR", "read size must be >= 1"));
          }
          const buf = Buffer.alloc(size);
          const n = fs.readSync(target.fd, buf, 0, size, null);
          if (n === 0) return EOF_SENTINEL;
          const bytes = Array.from(buf.slice(0, n));
          if (isBinary) return bytes.map((b) => BigInt(b));
          const s = decodeUtf8Strict(file, m, bytes, target.atStart);
          target.atStart = false;
          return s;
        }
        const chunks = [];
        const buf = Buffer.alloc(4096);
        while (true) {
          const n = fs.readSync(target.fd, buf, 0, buf.length, null);
          if (n === 0) break;
          chunks.push(...buf.slice(0, n));
        }
        if (isBinary) return chunks.map((b) => BigInt(b));
        const s = decodeUtf8Strict(file, m, chunks, target.atStart);
        target.atStart = false;
        return s;
      }
      if (m.name === "write") {
        if (!canWrite) throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_IO_ERROR", "file not writable"));
        const v = args[0];
        if (!isBinary) {
          if (typeof v !== "string") {
            throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_IO_ERROR", "write expects string"));
          }
          if (v.length > 0) fs.writeSync(target.fd, v);
          return null;
        }
        if (!Array.isArray(v)) {
          throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_IO_ERROR", "write expects list<byte>"));
        }
        const bytes = [];
        for (const it of v) {
          const n = typeof it === "bigint" ? Number(it) : Number(it);
          if (!Number.isInteger(n) || n < 0 || n > 255) {
            throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_IO_ERROR", "byte out of range"));
          }
          bytes.push(n);
        }
        if (bytes.length > 0) fs.writeSync(target.fd, Buffer.from(bytes));
        return null;
      }
    }

    if (target && target.__module && moduleEnv) {
      const mod = moduleEnv.modules.get(target.__module);
      if (!mod) throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_MODULE_ERROR", "module not found"));
      if (mod.error) throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_MODULE_ERROR", mod.error));
      const fn = mod.functions.get(m.name);
      if (!fn) throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_MODULE_ERROR", "symbol not found"));
      return fn(...args, m);
    }

    if (m.name === "pop" && Array.isArray(target)) {
      if (target.length === 0) {
        throw new RuntimeError(rdiag(file, m.target, "R1006", "RUNTIME_EMPTY_POP", "pop on empty list"));
      }
      return target.pop();
    }
  }

  return null;
}

module.exports = {
  runProgram,
  RuntimeError,
  isExceptionValue,
  valueToString,
};
