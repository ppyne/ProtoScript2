"use strict";

const fs = require("fs");
const path = require("path");
const debugNode = require("../debug_node");
const crypto = require("crypto");
const childProcess = require("child_process");
const os = require("os");

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
  protos.set("Object", {
    name: "Object",
    parent: null,
    sealed: false,
    fields: [],
    methods: new Map([
      ["clone", { name: "clone", params: [], retType: { kind: "NamedType", name: "Object" } }],
    ]),
  });
  protos.set("Exception", {
    name: "Exception",
    parent: "Object",
    sealed: false,
    fields: [
      { name: "file", type: { kind: "PrimitiveType", name: "string" } },
      { name: "line", type: { kind: "PrimitiveType", name: "int" } },
      { name: "column", type: { kind: "PrimitiveType", name: "int" } },
      { name: "message", type: { kind: "PrimitiveType", name: "string" } },
      { name: "cause", type: { kind: "NamedType", name: "Exception" } },
    ],
    methods: new Map(),
  });
  protos.set("RuntimeException", {
    name: "RuntimeException",
    parent: "Exception",
    sealed: false,
    fields: [
      { name: "code", type: { kind: "PrimitiveType", name: "string" } },
      { name: "category", type: { kind: "PrimitiveType", name: "string" } },
    ],
    methods: new Map(),
  });
  protos.set("CivilDateTime", {
    name: "CivilDateTime",
    parent: "Object",
    sealed: false,
    fields: [
      { name: "year", type: { kind: "PrimitiveType", name: "int" } },
      { name: "month", type: { kind: "PrimitiveType", name: "int" } },
      { name: "day", type: { kind: "PrimitiveType", name: "int" } },
      { name: "hour", type: { kind: "PrimitiveType", name: "int" } },
      { name: "minute", type: { kind: "PrimitiveType", name: "int" } },
      { name: "second", type: { kind: "PrimitiveType", name: "int" } },
      { name: "millisecond", type: { kind: "PrimitiveType", name: "int" } },
    ],
    methods: new Map([
      ["year", { name: "year", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["month", { name: "month", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["day", { name: "day", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["hour", { name: "hour", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["minute", { name: "minute", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["second", { name: "second", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["millisecond", { name: "millisecond", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["setYear", { name: "setYear", params: [{ type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "PrimitiveType", name: "void" } }],
      ["setMonth", { name: "setMonth", params: [{ type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "PrimitiveType", name: "void" } }],
      ["setDay", { name: "setDay", params: [{ type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "PrimitiveType", name: "void" } }],
      ["setHour", { name: "setHour", params: [{ type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "PrimitiveType", name: "void" } }],
      ["setMinute", { name: "setMinute", params: [{ type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "PrimitiveType", name: "void" } }],
      ["setSecond", { name: "setSecond", params: [{ type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "PrimitiveType", name: "void" } }],
      ["setMillisecond", { name: "setMillisecond", params: [{ type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "PrimitiveType", name: "void" } }],
    ]),
  });
  protos.set("PathInfo", {
    name: "PathInfo",
    parent: "Object",
    sealed: true,
    fields: [
      { name: "dirname", type: { kind: "PrimitiveType", name: "string" } },
      { name: "basename", type: { kind: "PrimitiveType", name: "string" } },
      { name: "filename", type: { kind: "PrimitiveType", name: "string" } },
      { name: "extension", type: { kind: "PrimitiveType", name: "string" } },
    ],
    methods: new Map([
      ["dirname", { name: "dirname", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
      ["basename", { name: "basename", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
      ["filename", { name: "filename", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
      ["extension", { name: "extension", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
    ]),
  });
  protos.set("PathEntry", {
    name: "PathEntry",
    parent: "Object",
    sealed: true,
    fields: [
      { name: "path", type: { kind: "PrimitiveType", name: "string" } },
      { name: "name", type: { kind: "PrimitiveType", name: "string" } },
      { name: "depth", type: { kind: "PrimitiveType", name: "int" } },
      { name: "isDir", type: { kind: "PrimitiveType", name: "bool" } },
      { name: "isFile", type: { kind: "PrimitiveType", name: "bool" } },
      { name: "isSymlink", type: { kind: "PrimitiveType", name: "bool" } },
    ],
    methods: new Map([
      ["path", { name: "path", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
      ["name", { name: "name", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
      ["depth", { name: "depth", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["isDir", { name: "isDir", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["isFile", { name: "isFile", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["isSymlink", { name: "isSymlink", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
    ]),
  });
  protos.set("TextFile", {
    name: "TextFile",
    parent: "Object",
    sealed: true,
    fields: [],
    methods: new Map([
      ["read", { name: "read", params: [{ name: "size", type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "PrimitiveType", name: "string" } }],
      ["write", { name: "write", params: [{ name: "text", type: { kind: "PrimitiveType", name: "string" } }], retType: { kind: "PrimitiveType", name: "void" } }],
      ["tell", { name: "tell", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["seek", { name: "seek", params: [{ name: "pos", type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "PrimitiveType", name: "void" } }],
      ["size", { name: "size", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["name", { name: "name", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
      ["close", { name: "close", params: [], retType: { kind: "PrimitiveType", name: "void" } }],
    ]),
  });
  protos.set("BinaryFile", {
    name: "BinaryFile",
    parent: "Object",
    sealed: true,
    fields: [],
    methods: new Map([
      ["read", { name: "read", params: [{ name: "size", type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "GenericType", name: "list", args: [{ kind: "PrimitiveType", name: "byte" }] } }],
      ["write", { name: "write", params: [{ name: "bytes", type: { kind: "GenericType", name: "list", args: [{ kind: "PrimitiveType", name: "byte" }] } }], retType: { kind: "PrimitiveType", name: "void" } }],
      ["tell", { name: "tell", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["seek", { name: "seek", params: [{ name: "pos", type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "PrimitiveType", name: "void" } }],
      ["size", { name: "size", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["name", { name: "name", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
      ["close", { name: "close", params: [], retType: { kind: "PrimitiveType", name: "void" } }],
    ]),
  });
  protos.set("Dir", {
    name: "Dir",
    parent: "Object",
    sealed: true,
    fields: [],
    methods: new Map([
      ["hasNext", { name: "hasNext", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["next", { name: "next", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
      ["close", { name: "close", params: [], retType: { kind: "PrimitiveType", name: "void" } }],
      ["reset", { name: "reset", params: [], retType: { kind: "PrimitiveType", name: "void" } }],
    ]),
  });
  protos.set("Walker", {
    name: "Walker",
    parent: "Object",
    sealed: true,
    fields: [],
    methods: new Map([
      ["hasNext", { name: "hasNext", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["next", { name: "next", params: [], retType: { kind: "NamedType", name: "PathEntry" } }],
      ["close", { name: "close", params: [], retType: { kind: "PrimitiveType", name: "void" } }],
    ]),
  });
  protos.set("ProcessEvent", {
    name: "ProcessEvent",
    parent: "Object",
    sealed: true,
    fields: [
      { name: "stream", type: { kind: "PrimitiveType", name: "int" } },
      { name: "data", type: { kind: "GenericType", name: "list", args: [{ kind: "PrimitiveType", name: "byte" }] } },
    ],
    methods: new Map([
      ["stream", { name: "stream", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["data", { name: "data", params: [], retType: { kind: "GenericType", name: "list", args: [{ kind: "PrimitiveType", name: "byte" }] } }],
    ]),
  });
  protos.set("ProcessResult", {
    name: "ProcessResult",
    parent: "Object",
    sealed: true,
    fields: [
      { name: "exitCode", type: { kind: "PrimitiveType", name: "int" } },
      { name: "events", type: { kind: "GenericType", name: "list", args: [{ kind: "NamedType", name: "ProcessEvent" }] } },
    ],
    methods: new Map([
      ["exitCode", { name: "exitCode", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["events", { name: "events", params: [], retType: { kind: "GenericType", name: "list", args: [{ kind: "NamedType", name: "ProcessEvent" }] } }],
    ]),
  });
  protos.set("RegExpMatch", {
    name: "RegExpMatch",
    parent: "Object",
    sealed: true,
    fields: [
      { name: "ok", type: { kind: "PrimitiveType", name: "bool" } },
      { name: "start", type: { kind: "PrimitiveType", name: "int" } },
      { name: "end", type: { kind: "PrimitiveType", name: "int" } },
      { name: "groups", type: { kind: "GenericType", name: "list", args: [{ kind: "PrimitiveType", name: "string" }] } },
    ],
    methods: new Map([
      ["ok", { name: "ok", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["start", { name: "start", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["end", { name: "end", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
      ["groups", { name: "groups", params: [], retType: { kind: "GenericType", name: "list", args: [{ kind: "PrimitiveType", name: "string" }] } }],
    ]),
  });
  protos.set("RegExp", {
    name: "RegExp",
    parent: "Object",
    sealed: true,
    fields: [],
    methods: new Map([
      ["compile", { name: "compile", params: [{ type: { kind: "PrimitiveType", name: "string" } }, { type: { kind: "PrimitiveType", name: "string" } }], retType: { kind: "NamedType", name: "RegExp" } }],
      ["test", { name: "test", params: [{ type: { kind: "PrimitiveType", name: "string" } }, { type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["find", { name: "find", params: [{ type: { kind: "PrimitiveType", name: "string" } }, { type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "NamedType", name: "RegExpMatch" } }],
      ["findAll", { name: "findAll", params: [{ type: { kind: "PrimitiveType", name: "string" } }, { type: { kind: "PrimitiveType", name: "int" } }, { type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "GenericType", name: "list", args: [{ kind: "NamedType", name: "RegExpMatch" }] } }],
      ["replaceFirst", { name: "replaceFirst", params: [{ type: { kind: "PrimitiveType", name: "string" } }, { type: { kind: "PrimitiveType", name: "string" } }, { type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "PrimitiveType", name: "string" } }],
      ["replaceAll", { name: "replaceAll", params: [{ type: { kind: "PrimitiveType", name: "string" } }, { type: { kind: "PrimitiveType", name: "string" } }, { type: { kind: "PrimitiveType", name: "int" } }, { type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "PrimitiveType", name: "string" } }],
      ["split", { name: "split", params: [{ type: { kind: "PrimitiveType", name: "string" } }, { type: { kind: "PrimitiveType", name: "int" } }, { type: { kind: "PrimitiveType", name: "int" } }], retType: { kind: "GenericType", name: "list", args: [{ kind: "PrimitiveType", name: "string" }] } }],
      ["pattern", { name: "pattern", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
      ["flags", { name: "flags", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
    ]),
  });
  protos.set("JSONValue", {
    name: "JSONValue",
    parent: "Object",
    sealed: true,
    fields: [],
    methods: new Map([
      ["isNull", { name: "isNull", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["isBool", { name: "isBool", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["isNumber", { name: "isNumber", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["isString", { name: "isString", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["isArray", { name: "isArray", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["isObject", { name: "isObject", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["asBool", { name: "asBool", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["asNumber", { name: "asNumber", params: [], retType: { kind: "PrimitiveType", name: "float" } }],
      ["asString", { name: "asString", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
      ["asArray", { name: "asArray", params: [], retType: { kind: "GenericType", name: "list", args: [{ kind: "NamedType", name: "JSONValue" }] } }],
      ["asObject", { name: "asObject", params: [], retType: { kind: "GenericType", name: "map", args: [{ kind: "PrimitiveType", name: "string" }, { kind: "NamedType", name: "JSONValue" }] } }],
    ]),
  });
  const timeExceptionNames = [
    "DSTAmbiguousTimeException",
    "DSTNonExistentTimeException",
    "InvalidTimeZoneException",
    "InvalidDateException",
    "InvalidISOFormatException",
  ];
  for (const name of timeExceptionNames) {
    protos.set(name, { name, parent: "Exception", fields: [], methods: new Map(), sealed: false });
  }
  const ioExceptionNames = [
    "InvalidModeException",
    "FileOpenException",
    "FileNotFoundException",
    "PermissionDeniedException",
    "InvalidPathException",
    "FileClosedException",
    "InvalidArgumentException",
    "ProcessCreationException",
    "ProcessExecutionException",
    "ProcessPermissionException",
    "InvalidExecutableException",
    "EnvironmentAccessException",
    "InvalidEnvironmentNameException",
    "InvalidGlyphPositionException",
    "ReadFailureException",
    "WriteFailureException",
    "Utf8DecodeException",
    "StandardStreamCloseException",
    "IOException",
  ];
  const fsExceptionNames = [
    "NotADirectoryException",
    "NotAFileException",
    "DirectoryNotEmptyException",
  ];
  for (const name of ioExceptionNames) {
    protos.set(name, { name, parent: "RuntimeException", fields: [], methods: new Map(), sealed: false });
  }
  for (const name of fsExceptionNames) {
    protos.set(name, { name, parent: "RuntimeException", fields: [], methods: new Map(), sealed: false });
  }
  for (const d of ast.decls || []) {
    if (d.kind !== "PrototypeDecl") continue;
    const fields = [];
    for (const f of d.fields || []) fields.push({ name: f.name, type: f.type, init: f.init || null, isConst: !!f.isConst });
    const methods = new Map();
    for (const m of d.methods || []) methods.set(m.name, m);
    protos.set(d.name, { name: d.name, parent: d.parent || "Object", fields, methods, sealed: !!d.sealed });
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

function isProtoSubtype(protos, child, parent) {
  if (!child || !parent) return false;
  if (child === parent) return true;
  let cur = protos.get(child);
  while (cur && cur.parent) {
    if (cur.parent === parent) return true;
    cur = cur.parent ? protos.get(cur.parent) : null;
  }
  return false;
}

function isExceptionProto(protos, name) {
  return isProtoSubtype(protos, name, "Exception");
}

function isRuntimeExceptionProto(protos, name) {
  return isProtoSubtype(protos, name, "RuntimeException");
}

function typeNodeToRuntimeTypeName(t) {
  if (!t) return "unknown";
  if (t.kind === "PrimitiveType" || t.kind === "NamedType" || t.kind === "BuiltinType") return t.name || "unknown";
  if (t.kind === "GenericType") {
    const args = Array.isArray(t.args) ? t.args.map((a) => typeNodeToRuntimeTypeName(a)) : [];
    return `${t.name || "unknown"}<${args.join(",")}>`;
  }
  return "unknown";
}

function variadicViewTypeName(typeNode) {
  const declared = typeNodeToRuntimeTypeName(typeNode);
  if (declared.startsWith("list<")) return `view<${declared.slice(5, -1)}>`;
  return declared.startsWith("view<") ? declared : "view<unknown>";
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
    if (t.name === "TextFile" || t.name === "BinaryFile") return null;
    return null;
  }
  if (t.kind === "GenericType") {
    if (t.name === "list") {
      const list = makeList([]);
      list.__type = typeNodeToRuntimeTypeName(t);
      return list;
    }
    if (t.name === "map") {
      const map = new Map();
      map.__type = typeNodeToRuntimeTypeName(t);
      return map;
    }
    if (t.name === "view" || t.name === "slice") return { __view: true, source: [], offset: 0, len: 0, readonly: t.name === "view" };
  }
  return null;
}

function clonePrototype(protos, name, hooks = null) {
  const fields = collectPrototypeFields(protos, name);
  const core = new Set(["file", "line", "column", "message", "cause", "code", "category"]);
  const evalInit = (f) => {
    if (!f || !f.init || !hooks || !hooks.functions || !hooks.protoEnv || !hooks.callFunction) {
      return defaultValueForTypeNode(protos, f ? f.type : null);
    }
    const initScope = new Scope(null);
    return evalExpr(
      f.init,
      initScope,
      hooks.functions,
      hooks.moduleEnv || null,
      hooks.protoEnv,
      hooks.file || "<init>",
      hooks.callFunction
    );
  };
  if (isExceptionProto(protos, name)) {
    const ex = makeExceptionValue({ type: name });
    ex.__object = true;
    ex.__proto = name;
    ex.__fields = Object.create(null);
    ex.__constFields = new Set();
    for (const f of fields) {
      if (core.has(f.name)) continue;
      ex.__fields[f.name] = evalInit(f);
      if (f.isConst) ex.__constFields.add(f.name);
    }
    return ex;
  }
  const obj = { __object: true, __proto: name, __fields: Object.create(null), __constFields: new Set() };
  for (const f of fields) {
    obj.__fields[f.name] = evalInit(f);
    if (f.isConst) obj.__constFields.add(f.name);
  }
  return obj;
}

function objectCloneDefault(protos, receiver, hooks = null) {
  let protoName = null;
  if (receiver && receiver.__proto_desc && typeof receiver.name === "string") {
    protoName = receiver.name;
  } else if (isObjectInstance(receiver) && typeof receiver.__proto === "string") {
    protoName = receiver.__proto;
  }
  if (!protoName || !protos.has(protoName)) {
    throw new RuntimeError(
      rdiag(
        hooks && hooks.file ? hooks.file : "<runtime>",
        hooks && hooks.node ? hooks.node : null,
        "R1010",
        "RUNTIME_TYPE_ERROR",
        "clone expects prototype or instance receiver"
      )
    );
  }
  const nonCloneableHandles = [
    "TextFile",
    "BinaryFile",
    "Dir",
    "Walker",
    "RegExp",
    "PathInfo",
    "PathEntry",
    "RegExpMatch",
    "ProcessEvent",
    "ProcessResult",
  ];
  const handleBase = nonCloneableHandles.find((base) => isProtoSubtype(protos, protoName, base));
  if (handleBase) {
    throw new RuntimeError(
      rdiag(
        hooks && hooks.file ? hooks.file : "<runtime>",
        hooks && hooks.node ? hooks.node : null,
        "R1013",
        "RUNTIME_CLONE_NOT_SUPPORTED",
        `clone not supported for builtin handle ${handleBase}`
      )
    );
  }
  return clonePrototype(protos, protoName, hooks);
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
    __object: true,
    __proto: opts.type || "Exception",
    __fields: Object.create(null),
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

function makeIoException(type, file, node, message) {
  return setExceptionLocation(
    makeExceptionValue({
      type,
      message: typeof message === "string" ? message : "",
    }),
    file,
    node
  );
}

function throwIoException(type, file, node, message) {
  throw makeIoException(type, file, node, message);
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

function exceptionMatches(ex, typeNode, protoEnv) {
  if (!isExceptionValue(ex)) return false;
  if (!typeNode || typeNode.kind !== "NamedType") return false;
  const t = typeNode.name;
  if (t === "Exception") return true;
  if (!protoEnv) {
    if (t === "RuntimeException") return ex.type === "RuntimeException";
    return ex.type === t;
  }
  return isProtoSubtype(protoEnv, ex.type, t);
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

class BreakSignal {}
class ContinueSignal {}

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

function jsonArraySnapshot(items) {
  return makeList(Array.isArray(items) ? items.slice() : []);
}

function jsonObjectSnapshot(members) {
  const out = new Map();
  if (members instanceof Map) {
    for (const [k, v] of members.entries()) out.set(k, v);
  }
  return out;
}

function defaultDiagParts(category) {
  switch (category) {
    case "RUNTIME_INDEX_OOB":
      return { got: "index", expected: "index within bounds" };
    case "RUNTIME_MISSING_KEY":
      return { got: "key", expected: "present key" };
    case "RUNTIME_DIVIDE_BY_ZERO":
      return { got: "0", expected: "non-zero divisor" };
    case "RUNTIME_SHIFT_RANGE":
      return { got: "shift amount", expected: "0..width-1" };
    case "RUNTIME_INT_OVERFLOW":
      return { got: "value", expected: "value within int range" };
    case "RUNTIME_BYTE_RANGE":
      return { got: "value", expected: "0..255" };
    case "RUNTIME_EMPTY_POP":
      return { got: "empty list", expected: "non-empty list" };
    case "RUNTIME_VIEW_INVALID":
      return { got: "invalidated view", expected: "valid view" };
    case "RUNTIME_INVALID_UTF8":
      return { got: "byte stream", expected: "valid UTF-8" };
    case "RUNTIME_TYPE_ERROR":
      return { got: "value", expected: "compatible type" };
    case "RUNTIME_IO_ERROR":
      return { got: "I/O operation", expected: "valid file state" };
    case "RUNTIME_JSON_ERROR":
      return { got: "JSON value", expected: "expected JSON type" };
    case "RUNTIME_MODULE_ERROR":
      return { got: "module or symbol", expected: "available module/symbol" };
    default:
      return null;
  }
}

function rdiag(file, node, code, category, message) {
  let msg = message;
  if (typeof msg === "string" && !msg.includes("got ") && !msg.includes("; expected")) {
    const parts = defaultDiagParts(category);
    if (parts) msg = diagMsg(msg, parts.got, parts.expected);
  }
  return {
    file,
    line: node && node.line ? node.line : 1,
    col: node && node.col ? node.col : 1,
    code,
    category,
    message: msg,
  };
}

function diagMsg(shortMsg, got, expected) {
  const s = shortMsg && shortMsg.length > 0 ? shortMsg : "runtime error";
  if (got && expected) return `${s}. got ${got}; expected ${expected}`;
  if (got) return `${s}. got ${got}`;
  if (expected) return `${s}. expected ${expected}`;
  return s;
}

function valueType(v) {
  if (isExceptionValue(v)) return v.type || "Exception";
  if (v === null || v === undefined) return "null";
  if (v instanceof TextFile) return "TextFile";
  if (v instanceof BinaryFile) return "BinaryFile";
  if (isView(v)) return v.readonly ? "view" : "slice";
  if (Array.isArray(v)) return "list";
  if (v instanceof Map) return "map";
  if (isObjectInstance(v)) return "object";
  if (isGlyph(v)) return "glyph";
  if (typeof v === "bigint") return "int";
  if (typeof v === "number") return "float";
  if (typeof v === "boolean") return "bool";
  if (typeof v === "string") return "string";
  if (isJsonValue(v)) return "JSONValue";
  return "value";
}

function valueShort(v) {
  if (v === null || v === undefined) return "null";
  if (typeof v === "boolean") return v ? "true" : "false";
  if (typeof v === "bigint" || typeof v === "number") return String(v);
  if (typeof v === "string") {
    const s = v.length > 32 ? `${v.slice(0, 29)}...` : v;
    return `"${s.replace(/[\r\n\t]/g, " ")}"`;
  }
  if (isGlyph(v)) return `U+${v.value.toString(16).toUpperCase().padStart(4, "0")}`;
  if (isExceptionValue(v)) return v.type || "Exception";
  return `<${valueType(v)}>`;
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

function compareUtf8Bytes(a, b) {
  const ml = Math.min(a.length, b.length);
  for (let i = 0; i < ml; i += 1) {
    const av = a[i];
    const bv = b[i];
    if (av !== bv) return av < bv ? -1 : 1;
  }
  return a.length < b.length ? -1 : a.length > b.length ? 1 : 0;
}

function stableSortInPlace(arr, cmp) {
  const n = arr.length;
  if (n < 2) return;
  const buf = new Array(n);
  for (let width = 1; width < n; width *= 2) {
    for (let left = 0; left < n; left += 2 * width) {
      const mid = Math.min(left + width, n);
      const right = Math.min(left + 2 * width, n);
      let i = left;
      let j = mid;
      let k = left;
      while (i < mid && j < right) {
        if (cmp(arr[i], arr[j]) <= 0) buf[k++] = arr[i++];
        else buf[k++] = arr[j++];
      }
      while (i < mid) buf[k++] = arr[i++];
      while (j < right) buf[k++] = arr[j++];
    }
    for (let i = 0; i < n; i += 1) arr[i] = buf[i];
  }
}

function glyphAt(s, idx) {
  const ch = glyphStringsOf(s)[idx];
  if (ch === undefined) return null;
  return new Glyph(ch.codePointAt(0));
}

function isView(v) {
  return v && v.__view === true;
}

const listVersion = new WeakMap();

function getListVersion(list) {
  return listVersion.get(list) ?? 0;
}

function initList(list) {
  if (Array.isArray(list) && !listVersion.has(list)) listVersion.set(list, 0);
  return list;
}

function bumpList(list) {
  if (!Array.isArray(list)) return;
  listVersion.set(list, getListVersion(list) + 1);
}

function ensureViewValid(file, node, view) {
  if (!isView(view)) return;
  if (Array.isArray(view.source)) {
    if (getListVersion(view.source) !== view.version) {
      throw new RuntimeError(
        rdiag(file, node, "R1012", "RUNTIME_VIEW_INVALID", diagMsg("view invalidated", "invalidated view", "valid view"))
      );
    }
  }
}

function makeList(items) {
  return initList(items ?? []);
}

function makeView(file, node, source, offset, len, readonly) {
  const off = Number(offset);
  const ln = Number(len);
  if (!Number.isInteger(off) || !Number.isInteger(ln) || off < 0 || ln < 0) {
    throw new RuntimeError(
      rdiag(file, node, "R1002", "RUNTIME_INDEX_OOB", diagMsg("index out of bounds", "offset/length", "valid range"))
    );
  }
  let base = source;
  let baseOffset = 0;
  if (isView(source)) {
    ensureViewValid(file, node, source);
    base = source.source;
    baseOffset = source.offset;
  }
  let totalLen = 0;
  if (Array.isArray(base)) totalLen = base.length;
  else if (typeof base === "string") totalLen = glyphStringsOf(base).length;
  else if (isView(base)) totalLen = base.len;
  if (off + ln > totalLen) {
    throw new RuntimeError(
      rdiag(file, node, "R1002", "RUNTIME_INDEX_OOB", diagMsg("index out of bounds", "offset/length", "within source"))
    );
  }
  return {
    __view: true,
    source: base,
    offset: baseOffset + off,
    len: ln,
    readonly: !!readonly,
    version: Array.isArray(base) ? getListVersion(base) : 0,
  };
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

function lastIndexOfGlyphs(hay, needle) {
  const h = glyphStringsOf(hay);
  const n = glyphStringsOf(needle);
  if (n.length === 0) return h.length;
  for (let i = h.length - n.length; i >= 0; i -= 1) {
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

function replaceAllGlyphs(hay, oldValue, newValue) {
  const h = glyphStringsOf(hay);
  const o = glyphStringsOf(oldValue);
  const n = glyphStringsOf(newValue);
  if (o.length === 0) return null;
  const out = [];
  let i = 0;
  let replaced = false;
  while (i < h.length) {
    let match = i + o.length <= h.length;
    if (match) {
      for (let j = 0; j < o.length; j += 1) {
        if (h[i + j] !== o[j]) {
          match = false;
          break;
        }
      }
    }
    if (match) {
      replaced = true;
      for (const g of n) out.push(g);
      i += o.length;
    } else {
      out.push(h[i]);
      i += 1;
    }
  }
  if (!replaced) return hay;
  return out.join("");
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
    throw new RuntimeError(
      rdiag(file, node, "R1001", "RUNTIME_INT_OVERFLOW", diagMsg("int overflow", "value", "value within int range"))
    );
  }
  return n;
}

function checkByteRange(file, node, n) {
  if (n < 0n || n > 255n) {
    throw new RuntimeError(rdiag(file, node, "R1008", "RUNTIME_BYTE_RANGE", diagMsg("byte out of range", "value", "0..255")));
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
  return makeList(Array.from(new Uint8Array(buf), (b) => BigInt(b)));
}

function floatToBytes(x) {
  const buf = new ArrayBuffer(8);
  const view = new DataView(buf);
  view.setFloat64(0, Number(x), IS_LITTLE_ENDIAN);
  return makeList(Array.from(new Uint8Array(buf), (b) => BigInt(b)));
}

function parseIntStrict(file, node, s) {
  if (!/^[+-]?(?:0|[1-9][0-9]*)$/.test(s)) {
    throw new RuntimeError(
      rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", diagMsg("invalid int format", "string", "int literal"))
    );
  }
  return checkIntRange(file, node, BigInt(s));
}

function parseFloatStrict(file, node, s) {
  if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(s)) {
    throw new RuntimeError(
      rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", diagMsg("invalid float format", "string", "float literal"))
    );
  }
  const v = Number(s);
  if (Number.isNaN(v)) {
    throw new RuntimeError(
      rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", diagMsg("invalid float format", "string", "float literal"))
    );
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

  has(name) {
    if (this.values.has(name)) return true;
    if (this.parent) return this.parent.has(name);
    return false;
  }
}

function loadModuleRegistry() {
  const env = process.env.PS_MODULE_REGISTRY;
  const cliDir = process.argv[1] ? path.dirname(process.argv[1]) : null;
  const candidates = [];
  if (env) candidates.push(env);
  if (cliDir) candidates.push(path.join(cliDir, "registry.json"));
  candidates.push(path.join(__dirname, "registry.json"));
  candidates.push(path.join(process.cwd(), "registry.json"));
  candidates.push("/etc/ps/registry.json");
  candidates.push("/usr/local/etc/ps/registry.json");
  candidates.push("/opt/local/etc/ps/registry.json");
  candidates.push(path.join(process.cwd(), "modules", "registry.json"));
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
  constructor(fd, flags, isStd = false, path = "") {
    this.fd = fd;
    this.flags = flags;
    this.isStd = isStd;
    this.closed = false;
    this.path = path;
    this.posBytes = 0;
  }
}

class TextFile extends IoFile {
  constructor(fd, flags, isStd = false, path = "") {
    super(fd, flags, isStd, path);
    this.kind = "text";
  }
}

class BinaryFile extends IoFile {
  constructor(fd, flags, isStd = false, path = "") {
    super(fd, flags, isStd, path);
    this.kind = "binary";
  }
}

const PS_FILE_READ = 0x01;
const PS_FILE_WRITE = 0x02;
const PS_FILE_APPEND = 0x04;
const PS_FILE_BINARY = 0x08;


function decodeUtf8Strict(file, node, bytes) {
  for (const b of bytes) {
    if (b === 0) throwIoException("Utf8DecodeException", file, node, "invalid UTF-8 sequence");
  }
  try {
    const dec = new TextDecoder("utf-8", { fatal: true });
    return dec.decode(Uint8Array.from(bytes));
  } catch {
    throwIoException("Utf8DecodeException", file, node, "invalid UTF-8 sequence");
  }
}

function readByteAt(fd, pos, file, node) {
  try {
    const buf = Buffer.alloc(1);
    const n = fs.readSync(fd, buf, 0, 1, pos);
    if (n === 0) return null;
    return buf[0];
  } catch {
    throwIoException("ReadFailureException", file, node, "read failed");
  }
}

function writeAllAtomic(fd, buffer, position, file, node) {
  const isStd = position === null || position === undefined;
  let origSize = null;
  let backup = null;
  if (!isStd) {
    try {
      origSize = fs.fstatSync(fd).size;
      const maxBackup = Math.max(0, Math.min(buffer.length, origSize - position));
      if (maxBackup > 0) {
        backup = Buffer.alloc(maxBackup);
        fs.readSync(fd, backup, 0, maxBackup, position);
      }
    } catch {
      throwIoException("WriteFailureException", file, node, "write failed");
    }
  }
  let off = 0;
  try {
    while (off < buffer.length) {
      const n = fs.writeSync(fd, buffer, off, buffer.length - off, isStd ? null : position + off);
      if (n <= 0) throw new Error("write failed");
      off += n;
    }
  } catch {
    if (!isStd) {
      if (backup && backup.length > 0) {
        try {
          fs.writeSync(fd, backup, 0, backup.length, position);
        } catch {}
      }
      if (origSize !== null && position + buffer.length > origSize) {
        try {
          fs.ftruncateSync(fd, origSize);
        } catch {}
      }
    }
    throwIoException("WriteFailureException", file, node, "write failed");
  }
}

function readUtf8GlyphAt(fd, pos, file, node) {
  const b0 = readByteAt(fd, pos, file, node);
  if (b0 === null) return null;
  if (b0 === 0) {
    throwIoException("Utf8DecodeException", file, node, "invalid UTF-8 sequence");
  }
  let len = 0;
  let cp = 0;
  if (b0 < 0x80) {
    len = 1;
    cp = b0;
  } else if ((b0 & 0xe0) === 0xc0) {
    len = 2;
    cp = b0 & 0x1f;
  } else if ((b0 & 0xf0) === 0xe0) {
    len = 3;
    cp = b0 & 0x0f;
  } else if ((b0 & 0xf8) === 0xf0) {
    len = 4;
    cp = b0 & 0x07;
  } else {
    throwIoException("Utf8DecodeException", file, node, "invalid UTF-8 sequence");
  }
  const bytes = [b0];
  for (let i = 1; i < len; i += 1) {
    const bi = readByteAt(fd, pos + i, file, node);
    if (bi === null) {
      throwIoException("Utf8DecodeException", file, node, "invalid UTF-8 sequence");
    }
    if ((bi & 0xc0) !== 0x80) {
      throwIoException("Utf8DecodeException", file, node, "invalid UTF-8 sequence");
    }
    if (bi === 0) {
      throwIoException("Utf8DecodeException", file, node, "invalid UTF-8 sequence");
    }
    bytes.push(bi);
    cp = (cp << 6) | (bi & 0x3f);
  }
  if (len === 2 && cp < 0x80) {
    throwIoException("Utf8DecodeException", file, node, "invalid UTF-8 sequence");
  }
  if (len === 3 && cp < 0x800) {
    throwIoException("Utf8DecodeException", file, node, "invalid UTF-8 sequence");
  }
  if (len === 4 && (cp < 0x10000 || cp > 0x10ffff)) {
    throwIoException("Utf8DecodeException", file, node, "invalid UTF-8 sequence");
  }
  return { bytes, nextPos: pos + len };
}

function readTextGlyphs(fileObj, node, glyphCount) {
  let pos = fileObj.posBytes;
  const bytes = [];
  for (let i = 0; i < glyphCount; i += 1) {
    const g = readUtf8GlyphAt(fileObj.fd, pos, fileObj.path || "<file>", node);
    if (!g) break;
    bytes.push(...g.bytes);
    pos = g.nextPos;
  }
  if (bytes.length === 0) return { text: "", newPos: pos };
  const text = decodeUtf8Strict(fileObj.path || "<file>", node, bytes);
  return { text, newPos: pos };
}

function glyphIndexAtBytePos(fileObj, node) {
  const targetPos = fileObj.posBytes;
  let pos = 0;
  let glyphs = 0;
  if (targetPos === 0) return 0;
  while (true) {
    const g = readUtf8GlyphAt(fileObj.fd, pos, fileObj.path || "<file>", node);
    if (!g) break;
    pos = g.nextPos;
    glyphs += 1;
    if (pos === targetPos) return glyphs;
    if (pos > targetPos) {
      throwIoException("InvalidGlyphPositionException", fileObj.path || "<file>", node, "invalid tell position");
    }
  }
  if (targetPos === pos) return glyphs;
  throwIoException("InvalidGlyphPositionException", fileObj.path || "<file>", node, "tell out of range");
}

function glyphCountTotal(fileObj, node) {
  let pos = 0;
  let glyphs = 0;
  while (true) {
    const g = readUtf8GlyphAt(fileObj.fd, pos, fileObj.path || "<file>", node);
    if (!g) break;
    pos = g.nextPos;
    glyphs += 1;
  }
  return glyphs;
}

function byteOffsetForGlyph(fileObj, node, glyphPos) {
  if (glyphPos === 0) return 0;
  let pos = 0;
  let glyphs = 0;
  while (true) {
    const g = readUtf8GlyphAt(fileObj.fd, pos, fileObj.path || "<file>", node);
    if (!g) break;
    pos = g.nextPos;
    glyphs += 1;
    if (glyphs === glyphPos) return pos;
  }
  throwIoException("InvalidGlyphPositionException", fileObj.path || "<file>", node, "seek out of range");
}

function buildModuleEnv(ast, file, hooks = null) {
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
    throw new RuntimeError(
      rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", diagMsg("invalid argument", valueType(v), "float"))
    );
  };
  const toIntArg = (node, v) => {
    if (typeof v === "bigint") return v;
    throw new RuntimeError(
      rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", diagMsg("invalid argument", valueType(v), "int"))
    );
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
  mathMod.constants.set("INT_MAX", 9223372036854775807n);
  mathMod.constants.set("INT_MIN", -9223372036854775808n);
  mathMod.constants.set("INT_SIZE", 8n);
  mathMod.constants.set("FLOAT_DIG", 15n);
  mathMod.constants.set("FLOAT_EPSILON", 2.2204460492503131e-16);
  mathMod.constants.set("FLOAT_MIN", 2.2250738585072014e-308);
  mathMod.constants.set("FLOAT_MAX", 1.7976931348623157e+308);
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
  let ioTempSeq = 0;
  ioMod.constants.set("EOL", "\n");
  ioMod.constants.set("stdin", new TextFile(0, PS_FILE_READ, true, "stdin"));
  ioMod.constants.set("stdout", new TextFile(1, PS_FILE_WRITE, true, "stdout"));
  ioMod.constants.set("stderr", new TextFile(2, PS_FILE_WRITE, true, "stderr"));
  const ioThrow = (type, node, message) => {
    throwIoException(type, file, node, message);
  };
  const ioToString = (val, node) => {
    if (typeof val === "string") return val;
    if (typeof val === "bigint") return val.toString();
    if (typeof val === "number") return String(val);
    if (typeof val === "boolean") return val ? "true" : "false";
    if (isGlyph(val)) return String.fromCodePoint(val.value);
    if (isObjectInstance(val) && hooks && hooks.protoEnv && hooks.functions && hooks.callFunction) {
      const info = resolveProtoMethodRuntime(hooks.protoEnv, val.__proto, "toString");
      if (!info) ioThrow("InvalidArgumentException", node, "missing toString");
      const fn = hooks.functions.get(`${info.proto}.toString`);
      if (!fn) ioThrow("InvalidArgumentException", node, "missing toString");
      const out = hooks.callFunction(fn, [val]);
      if (typeof out !== "string") ioThrow("InvalidArgumentException", node, "toString must return string");
      return out;
    }
    ioThrow("InvalidArgumentException", node, "invalid argument");
    return "";
  };
  const ioTempDir = (node) => {
    const dir = process.env.TMPDIR && process.env.TMPDIR.length > 0 ? process.env.TMPDIR : "/tmp";
    if (!dir) {
      ioThrow("IOException", node, "temp dir unavailable");
    }
    return dir;
  };
  ioMod.functions.set("openText", (pathStr, modeStr, node) => {
    if (typeof pathStr !== "string") ioThrow("InvalidPathException", node, "invalid path");
    if (typeof modeStr !== "string") ioThrow("InvalidModeException", node, "invalid mode");
    const m = modeStr;
    const valid = ["r", "w", "a"];
    if (!valid.includes(m)) {
      ioThrow("InvalidModeException", node, "invalid mode");
    }
    let flags = 0;
    if (m === "r") flags = PS_FILE_READ;
    else if (m === "w") flags = PS_FILE_WRITE;
    else if (m === "a") flags = PS_FILE_APPEND | PS_FILE_WRITE;
    try {
      const fd = fs.openSync(pathStr, m);
      const f = new TextFile(fd, flags, false, pathStr);
      if (m === "a") {
        f.posBytes = fs.fstatSync(fd).size;
      }
      return f;
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      if (code === "ENOENT") ioThrow("FileNotFoundException", node, "file not found");
      if (code === "EACCES" || code === "EPERM") ioThrow("PermissionDeniedException", node, "permission denied");
      if (code === "ENOTDIR" || code === "EINVAL" || code === "ENAMETOOLONG") {
        ioThrow("InvalidPathException", node, "invalid path");
      }
      ioThrow("FileOpenException", node, "open failed");
    }
  });
  ioMod.functions.set("openBinary", (pathStr, modeStr, node) => {
    if (typeof pathStr !== "string") ioThrow("InvalidPathException", node, "invalid path");
    if (typeof modeStr !== "string") ioThrow("InvalidModeException", node, "invalid mode");
    const m = modeStr;
    const valid = ["r", "w", "a"];
    if (!valid.includes(m)) {
      ioThrow("InvalidModeException", node, "invalid mode");
    }
    let flags = 0;
    if (m === "r") flags = PS_FILE_READ;
    else if (m === "w") flags = PS_FILE_WRITE;
    else if (m === "a") flags = PS_FILE_APPEND | PS_FILE_WRITE;
    flags |= PS_FILE_BINARY;
    try {
      const openMode = m === "r" ? "r" : m === "w" ? "w" : "a";
      const fd = fs.openSync(pathStr, openMode);
      const f = new BinaryFile(fd, flags, false, pathStr);
      if (m === "a") {
        f.posBytes = fs.fstatSync(fd).size;
      }
      return f;
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      if (code === "ENOENT") ioThrow("FileNotFoundException", node, "file not found");
      if (code === "EACCES" || code === "EPERM") ioThrow("PermissionDeniedException", node, "permission denied");
      if (code === "ENOTDIR" || code === "EINVAL" || code === "ENAMETOOLONG") {
        ioThrow("InvalidPathException", node, "invalid path");
      }
      ioThrow("FileOpenException", node, "open failed");
    }
  });
  ioMod.functions.set("tempPath", (node) => {
    const dir = ioTempDir(node);
    const maxAttempts = 128;
    for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
      const nonce = crypto.randomUUID();
      const seq = ioTempSeq++;
      const name = `ps_${nonce}_${seq.toString(16)}`;
      const candidate = path.join(dir, name);
      try {
        if (!fs.existsSync(candidate)) return candidate;
      } catch (e) {
        ioThrow("IOException", node, "tempPath failed");
      }
    }
    ioThrow("IOException", node, "tempPath failed");
  });
  ioMod.functions.set("print", (val, node) => {
    const s = ioToString(val, node);
    const buf = Buffer.from(s, "utf8");
    let off = 0;
    while (off < buf.length) {
      try {
        const n = fs.writeSync(1, buf, off, buf.length - off);
        if (n <= 0) ioThrow("WriteFailureException", node, "write failed");
        off += n;
      } catch {
        ioThrow("WriteFailureException", node, "write failed");
      }
    }
    return null;
  });
  ioMod.functions.set("printLine", (val, node) => {
    const s = ioToString(val, node);
    const buf = Buffer.from(s + "\n", "utf8");
    let off = 0;
    while (off < buf.length) {
      try {
        const n = fs.writeSync(1, buf, off, buf.length - off);
        if (n <= 0) ioThrow("WriteFailureException", node, "write failed");
        off += n;
      } catch {
        ioThrow("WriteFailureException", node, "write failed");
      }
    }
    return null;
  });

  const debugMod = makeModule("Debug");
  debugMod.functions.set("dump", (val) => {
    const protoEnv = hooks && hooks.protoEnv ? hooks.protoEnv : buildPrototypeEnv({ decls: [] });
    const groupEnv = hooks && hooks.groupEnv ? hooks.groupEnv : null;
    debugNode.dump(val, { protoEnv, groups: groupEnv });
    return null;
  });

  const fsMod = makeModule("Fs");
  const fsThrow = (type, node, message) => {
    throw makeIoException(type, file, node, message);
  };
  const fsInvalidPathCodes = new Set(["ENAMETOOLONG", "EINVAL", "ELOOP"]);
  const fsPermissionCodes = new Set(["EACCES", "EPERM"]);
  const fsIsInvalidPath = (code) => fsInvalidPathCodes.has(code);
  const fsIsPermission = (code) => fsPermissionCodes.has(code);
  const fsJoin = (base, name) => {
    if (base.endsWith("/")) return base + name;
    return `${base}/${name}`;
  };
  const fsPathInfo = (p, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    const lastSlash = p.lastIndexOf("/");
    let dirname = "";
    let basename = p;
    if (lastSlash >= 0) {
      dirname = lastSlash === 0 ? "/" : p.slice(0, lastSlash);
      basename = p.slice(lastSlash + 1);
    }
    let filename = basename;
    let extension = "";
    const dot = basename.lastIndexOf(".");
    if (dot > 0 && dot < basename.length - 1) {
      filename = basename.slice(0, dot);
      extension = basename.slice(dot + 1);
    }
    const obj = clonePrototype(hooks && hooks.protoEnv ? hooks.protoEnv : buildPrototypeEnv({ decls: [] }), "PathInfo");
    obj.__fields.dirname = dirname;
    obj.__fields.basename = basename;
    obj.__fields.filename = filename;
    obj.__fields.extension = extension;
    return obj;
  };
  const fsThrowStatError = (node, code) => {
    if (code === "ENOENT") fsThrow("FileNotFoundException", node, "file not found");
    if (fsIsPermission(code)) fsThrow("PermissionDeniedException", node, "permission denied");
    if (code === "ENOTDIR" || fsIsInvalidPath(code)) fsThrow("InvalidPathException", node, "invalid path");
    fsThrow("IOException", node, "io failed");
  };
  const fsThrowOpenDirError = (node, code) => {
    if (code === "ENOENT") fsThrow("FileNotFoundException", node, "file not found");
    if (code === "ENOTDIR") fsThrow("NotADirectoryException", node, "not a directory");
    if (fsIsPermission(code)) fsThrow("PermissionDeniedException", node, "permission denied");
    if (fsIsInvalidPath(code)) fsThrow("InvalidPathException", node, "invalid path");
    fsThrow("IOException", node, "io failed");
  };
  const fsThrowDirOpError = (node, code) => {
    if (code === "ENOENT") fsThrow("FileNotFoundException", node, "file not found");
    if (code === "ENOTDIR") fsThrow("NotADirectoryException", node, "not a directory");
    if (code === "ENOTEMPTY" || code === "EEXIST") fsThrow("DirectoryNotEmptyException", node, "directory not empty");
    if (fsIsPermission(code)) fsThrow("PermissionDeniedException", node, "permission denied");
    if (fsIsInvalidPath(code)) fsThrow("InvalidPathException", node, "invalid path");
    fsThrow("IOException", node, "io failed");
  };
  const fsThrowFileOpError = (node, code) => {
    if (code === "ENOENT") fsThrow("FileNotFoundException", node, "file not found");
    if (code === "EISDIR") fsThrow("NotAFileException", node, "not a file");
    if (code === "ENOTDIR") fsThrow("NotADirectoryException", node, "not a directory");
    if (fsIsPermission(code)) fsThrow("PermissionDeniedException", node, "permission denied");
    if (fsIsInvalidPath(code)) fsThrow("InvalidPathException", node, "invalid path");
    fsThrow("IOException", node, "io failed");
  };
  const fsThrowCommonError = (node, code) => {
    if (code === "ENOENT") fsThrow("FileNotFoundException", node, "file not found");
    if (fsIsPermission(code)) fsThrow("PermissionDeniedException", node, "permission denied");
    if (code === "ENOTDIR" || fsIsInvalidPath(code)) fsThrow("InvalidPathException", node, "invalid path");
    fsThrow("IOException", node, "io failed");
  };
  const fsCopyFile = (src, dst, node) => {
    let srcFd = null;
    let dstFd = null;
    let tmpPath = null;
    const dir = dst.includes("/") ? dst.slice(0, dst.lastIndexOf("/")) : ".";
    let st = null;
    try {
      st = fs.statSync(src);
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      fsThrowStatError(node, code);
    }
    if (!st.isFile()) fsThrow("NotAFileException", node, "not a file");
    for (let attempt = 0; attempt < 16; attempt += 1) {
      const nonce = crypto.randomUUID();
      tmpPath = fsJoin(dir, `.ps_tmp_${nonce}`);
      try {
        dstFd = fs.openSync(tmpPath, "wx");
        break;
      } catch (e) {
        const code = e && e.code ? String(e.code) : "";
        if (code === "EEXIST") continue;
        fsThrowCommonError(node, code);
      }
    }
    if (dstFd === null) fsThrow("IOException", node, "io failed");
    try {
      srcFd = fs.openSync(src, "r");
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      try { fs.closeSync(dstFd); } catch {}
      try { fs.unlinkSync(tmpPath); } catch {}
      fsThrowCommonError(node, code);
    }
    const buf = Buffer.alloc(16384);
    try {
      while (true) {
        const n = fs.readSync(srcFd, buf, 0, buf.length, null);
        if (n === 0) break;
        let off = 0;
        while (off < n) {
          const w = fs.writeSync(dstFd, buf, off, n - off);
          if (w <= 0) throw new Error("write failed");
          off += w;
        }
      }
      fs.closeSync(srcFd);
      fs.closeSync(dstFd);
      fs.renameSync(tmpPath, dst);
    } catch (e) {
      try { if (srcFd !== null) fs.closeSync(srcFd); } catch {}
      try { if (dstFd !== null) fs.closeSync(dstFd); } catch {}
      try { if (tmpPath) fs.unlinkSync(tmpPath); } catch {}
      const code = e && e.code ? String(e.code) : "";
      fsThrowCommonError(node, code);
    }
  };
  fsMod.functions.set("exists", (p, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    try {
      fs.lstatSync(p);
      return true;
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      if (code === "ENOENT") return false;
      if (code === "ENOTDIR" || fsIsInvalidPath(code)) fsThrow("InvalidPathException", node, "invalid path");
      fsThrow("IOException", node, "io failed");
    }
  });
  fsMod.functions.set("isFile", (p, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    try {
      const st = fs.lstatSync(p);
      return st.isFile();
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      if (code === "ENOENT") return false;
      if (code === "ENOTDIR" || fsIsInvalidPath(code)) fsThrow("InvalidPathException", node, "invalid path");
      fsThrow("IOException", node, "io failed");
    }
  });
  fsMod.functions.set("isDir", (p, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    try {
      const st = fs.lstatSync(p);
      return st.isDirectory();
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      if (code === "ENOENT") return false;
      if (code === "ENOTDIR" || fsIsInvalidPath(code)) fsThrow("InvalidPathException", node, "invalid path");
      fsThrow("IOException", node, "io failed");
    }
  });
  fsMod.functions.set("isSymlink", (p, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    try {
      const st = fs.lstatSync(p);
      return st.isSymbolicLink();
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      if (code === "ENOENT") return false;
      if (code === "ENOTDIR" || fsIsInvalidPath(code)) fsThrow("InvalidPathException", node, "invalid path");
      fsThrow("IOException", node, "io failed");
    }
  });
  fsMod.functions.set("isReadable", (p, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    try {
      fs.accessSync(p, fs.constants.R_OK);
      return true;
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      if (code === "ENOENT") return false;
      if (fsIsPermission(code)) return false;
      if (code === "ENOTDIR" || fsIsInvalidPath(code)) fsThrow("InvalidPathException", node, "invalid path");
      fsThrow("IOException", node, "io failed");
    }
  });
  fsMod.functions.set("isWritable", (p, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    try {
      fs.accessSync(p, fs.constants.W_OK);
      return true;
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      if (code === "ENOENT") return false;
      if (fsIsPermission(code)) return false;
      if (code === "ENOTDIR" || fsIsInvalidPath(code)) fsThrow("InvalidPathException", node, "invalid path");
      fsThrow("IOException", node, "io failed");
    }
  });
  fsMod.functions.set("isExecutable", (p, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    try {
      fs.accessSync(p, fs.constants.X_OK);
      return true;
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      if (code === "ENOENT") return false;
      if (fsIsPermission(code)) return false;
      if (code === "ENOTDIR" || fsIsInvalidPath(code)) fsThrow("InvalidPathException", node, "invalid path");
      fsThrow("IOException", node, "io failed");
    }
  });
  fsMod.functions.set("size", (p, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    let st = null;
    try {
      st = fs.statSync(p);
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      fsThrowStatError(node, code);
    }
    if (!st.isFile()) fsThrow("NotAFileException", node, "not a file");
    return BigInt(st.size);
  });
  fsMod.functions.set("mkdir", (p, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    try {
      fs.mkdirSync(p);
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      if (code === "EEXIST") fsThrow("IOException", node, "io failed");
      if (code === "ENOTDIR") fsThrow("NotADirectoryException", node, "not a directory");
      if (code === "ENOENT") fsThrow("FileNotFoundException", node, "file not found");
      if (fsIsPermission(code)) fsThrow("PermissionDeniedException", node, "permission denied");
      if (fsIsInvalidPath(code)) fsThrow("InvalidPathException", node, "invalid path");
      fsThrow("IOException", node, "io failed");
    }
  });
  fsMod.functions.set("rmdir", (p, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    try {
      fs.rmdirSync(p);
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      fsThrowDirOpError(node, code);
    }
  });
  fsMod.functions.set("rm", (p, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    try {
      fs.unlinkSync(p);
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      fsThrowFileOpError(node, code);
    }
  });
  fsMod.functions.set("cp", (src, dst, node) => {
    if (typeof src !== "string" || src.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    if (typeof dst !== "string" || dst.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    fsCopyFile(src, dst, node);
  });
  fsMod.functions.set("mv", (src, dst, node) => {
    if (typeof src !== "string" || src.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    if (typeof dst !== "string" || dst.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    try {
      fs.renameSync(src, dst);
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      if (code === "EXDEV") fsThrow("IOException", node, "io failed");
      fsThrowCommonError(node, code);
    }
  });
  fsMod.functions.set("chmod", (p, mode, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    const m = typeof mode === "bigint" ? Number(mode) : Number(mode);
    if (!Number.isInteger(m)) fsThrow("IOException", node, "io failed");
    try {
      fs.chmodSync(p, m);
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      fsThrowCommonError(node, code);
    }
  });
  fsMod.functions.set("cwd", (node) => {
    try {
      return process.cwd();
    } catch {
      fsThrow("IOException", node, "io failed");
    }
  });
  fsMod.functions.set("cd", (p, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    try {
      process.chdir(p);
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      if (code === "ENOENT") fsThrow("FileNotFoundException", node, "file not found");
      if (code === "ENOTDIR") fsThrow("NotADirectoryException", node, "not a directory");
      if (fsIsPermission(code)) fsThrow("PermissionDeniedException", node, "permission denied");
      if (fsIsInvalidPath(code)) fsThrow("InvalidPathException", node, "invalid path");
      fsThrow("IOException", node, "io failed");
    }
  });
  fsMod.functions.set("pathInfo", (p, node) => fsPathInfo(p, node));
  fsMod.functions.set("openDir", (p, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    let handle = null;
    try {
      handle = fs.opendirSync(p);
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      fsThrowOpenDirError(node, code);
    }
    return { __fs_dir: true, path: p, handle, next: null, done: false, closed: false };
  });
  fsMod.functions.set("walk", (p, maxDepth, followSymlinks, node) => {
    if (typeof p !== "string" || p.length === 0) fsThrow("InvalidPathException", node, "invalid path");
    const md = typeof maxDepth === "bigint" ? Number(maxDepth) : Number(maxDepth);
    if (!Number.isInteger(md)) fsThrow("IOException", node, "io failed");
    const follow = !!followSymlinks;
    try {
      const dir = fs.opendirSync(p);
      return {
        __fs_walker: true,
        root: p,
        maxDepth: md,
        followSymlinks: follow,
        stack: [{ path: p, dir, depth: 0 }],
        next: null,
        closed: false,
      };
    } catch (e) {
      const code = e && e.code ? String(e.code) : "";
      fsThrowOpenDirError(node, code);
    }
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
    if (Array.isArray(v)) return makeJsonValue("array", makeList(v.map((it) => jsonDecodeValue(node, it))));
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
    return makeJsonValue("array", jsonArraySnapshot(items));
  });
  jsonMod.functions.set("object", (members, node) => {
    if (!(members instanceof Map)) jsonError(node, "object expects map<string,JSONValue>");
    for (const [k, v] of members.entries()) {
      const key = unmapKey(k);
      if (typeof key !== "string") jsonError(node, "object expects map<string,JSONValue>");
      if (!isJsonValue(v)) jsonError(node, "object expects map<string,JSONValue>");
    }
    return makeJsonValue("object", jsonObjectSnapshot(members));
  });

  const timeError = (type, node, message) => {
    const ex = makeExceptionValue({ type, message });
    setExceptionLocation(ex, file, node);
    throw ex;
  };

  const invalidTimeZone = (node, message) => timeError("InvalidTimeZoneException", node, message || "invalid time zone");
  const invalidDate = (node, message) => timeError("InvalidDateException", node, message || "invalid date");
  const invalidISO = (node, message) => timeError("InvalidISOFormatException", node, message || "invalid ISO 8601 format");
  const dstAmbiguous = (node, message) => timeError("DSTAmbiguousTimeException", node, message || "ambiguous DST time");
  const dstNonExistent = (node, message) => timeError("DSTNonExistentTimeException", node, message || "non-existent DST time");

  const timeZoneCache = new Map();
  const timeZoneIdRe = /^[A-Za-z0-9_+\-\/]+$/;

  const getTimeZoneFormat = (tz, node) => {
    if (typeof tz !== "string") {
      throw new RuntimeError(
        rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", diagMsg("invalid argument", valueType(tz), "string"))
      );
    }
    if (tz.length === 0 || tz.trim() !== tz || /\s/.test(tz)) invalidTimeZone(node);
    if (!timeZoneIdRe.test(tz)) invalidTimeZone(node);
    if (timeZoneCache.has(tz)) return timeZoneCache.get(tz);
    try {
      const dtf = new Intl.DateTimeFormat("en-GB", {
        timeZone: tz,
        year: "numeric",
        month: "2-digit",
        day: "2-digit",
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit",
        hour12: false,
        hourCycle: "h23",
      });
      const resolved = dtf.resolvedOptions().timeZone;
      if (
        typeof resolved === "string" &&
        resolved.length === tz.length &&
        resolved.toLowerCase() === tz.toLowerCase() &&
        resolved !== tz
      ) {
        invalidTimeZone(node);
      }
      timeZoneCache.set(tz, dtf);
      return dtf;
    } catch {
      invalidTimeZone(node);
      return null;
    }
  };

  const toEpochNumber = (node, v) => {
    const bi = toIntArg(node, v);
    const num = Number(bi);
    if (!Number.isFinite(num) || !Number.isSafeInteger(num)) invalidDate(node, "epoch out of range");
    return { bi, num };
  };

  const isLeapYearInt = (year) => {
    const y = typeof year === "bigint" ? year : BigInt(year);
    if (y % 400n === 0n) return true;
    if (y % 100n === 0n) return false;
    return y % 4n === 0n;
  };

  const daysInMonthInt = (year, month, node, errFn) => {
    const m = typeof month === "bigint" ? month : BigInt(month);
    if (m < 1n || m > 12n) {
      if (errFn) errFn(node, "invalid month");
      return 0n;
    }
    if (m === 2n) return isLeapYearInt(year) ? 29n : 28n;
    if (m === 4n || m === 6n || m === 9n || m === 11n) return 30n;
    return 31n;
  };

  const readCivilDateTime = (dt, node) => {
    if (!isObjectInstance(dt) || dt.__proto !== "CivilDateTime" || !dt.__fields) {
      throw new RuntimeError(
        rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", diagMsg("invalid argument", valueType(dt), "CivilDateTime"))
      );
    }
    const year = toIntArg(node, dt.__fields.year);
    const month = toIntArg(node, dt.__fields.month);
    const day = toIntArg(node, dt.__fields.day);
    const hour = toIntArg(node, dt.__fields.hour);
    const minute = toIntArg(node, dt.__fields.minute);
    const second = toIntArg(node, dt.__fields.second);
    const millisecond = toIntArg(node, dt.__fields.millisecond);
    return { year, month, day, hour, minute, second, millisecond };
  };

  const validateCivilParts = (node, parts, errFn) => {
    const y = parts.year;
    const mo = parts.month;
    const d = parts.day;
    const h = parts.hour;
    const mi = parts.minute;
    const s = parts.second;
    const ms = parts.millisecond;
    if (mo < 1n || mo > 12n) errFn(node, "invalid month");
    if (h < 0n || h > 23n) errFn(node, "invalid hour");
    if (mi < 0n || mi > 59n) errFn(node, "invalid minute");
    if (s < 0n || s > 59n) errFn(node, "invalid second");
    if (ms < 0n || ms > 999n) errFn(node, "invalid millisecond");
    const dim = daysInMonthInt(y, mo, node, errFn);
    if (d < 1n || d > dim) errFn(node, "invalid day");
    const yNum = Number(y);
    const moNum = Number(mo);
    const dNum = Number(d);
    const hNum = Number(h);
    const miNum = Number(mi);
    const sNum = Number(s);
    const msNum = Number(ms);
    if (
      !Number.isSafeInteger(yNum) ||
      !Number.isSafeInteger(moNum) ||
      !Number.isSafeInteger(dNum) ||
      !Number.isSafeInteger(hNum) ||
      !Number.isSafeInteger(miNum) ||
      !Number.isSafeInteger(sNum) ||
      !Number.isSafeInteger(msNum)
    ) {
      errFn(node, "date out of range");
    }
    return { yNum, moNum, dNum, hNum, miNum, sNum, msNum };
  };

  const getCivilPartsFromEpoch = (epochMs, epochBi, tz, node) => {
    const dtf = getTimeZoneFormat(tz, node);
    const date = new Date(epochMs);
    if (!Number.isFinite(date.getTime())) invalidDate(node, "invalid epoch");
    const parts = dtf.formatToParts(date);
    const out = Object.create(null);
    for (const p of parts) {
      if (p.type !== "literal") out[p.type] = p.value;
    }
    const year = Number(out.year);
    const month = Number(out.month);
    const day = Number(out.day);
    const hour = Number(out.hour);
    const minute = Number(out.minute);
    const second = Number(out.second);
    const ms = Number(((epochBi % 1000n) + 1000n) % 1000n);
    return { year, month, day, hour, minute, second, millisecond: ms };
  };

  const offsetSecondsForEpoch = (epochMs, epochBi, tz, node) => {
    const parts = getCivilPartsFromEpoch(epochMs, epochBi, tz, node);
    const utcMillis = Date.UTC(parts.year, parts.month - 1, parts.day, parts.hour, parts.minute, parts.second, parts.millisecond);
    if (!Number.isFinite(utcMillis)) invalidDate(node, "invalid epoch");
    return Math.trunc((utcMillis - epochMs) / 1000);
  };

  const makeCivilDateTime = (parts) => {
    return {
      __object: true,
      __proto: "CivilDateTime",
      __fields: {
        year: BigInt(parts.year),
        month: BigInt(parts.month),
        day: BigInt(parts.day),
        hour: BigInt(parts.hour),
        minute: BigInt(parts.minute),
        second: BigInt(parts.second),
        millisecond: BigInt(parts.millisecond),
      },
    };
  };

  const matchCivil = (epochMs, epochBi, tz, parts, node) => {
    const got = getCivilPartsFromEpoch(epochMs, epochBi, tz, node);
    return (
      got.year === parts.year &&
      got.month === parts.month &&
      got.day === parts.day &&
      got.hour === parts.hour &&
      got.minute === parts.minute &&
      got.second === parts.second &&
      got.millisecond === parts.millisecond
    );
  };

  const findEpochCandidates = (parts, tz, node) => {
    const baseEpoch = Date.UTC(parts.year, parts.month - 1, parts.day, parts.hour, parts.minute, parts.second, parts.millisecond);
    if (!Number.isFinite(baseEpoch) || !Number.isSafeInteger(baseEpoch)) invalidDate(node, "date out of range");
    const offsets = new Set();
    const sampleHours = [-48, -24, -12, 0, 12, 24, 48];
    for (const h of sampleHours) {
      const sample = baseEpoch + h * 3600 * 1000;
      const sampleBi = BigInt(sample);
      offsets.add(offsetSecondsForEpoch(sample, sampleBi, tz, node));
    }
    const candidates = new Set();
    for (const off of offsets) {
      const candidate = baseEpoch - off * 1000;
      const candBi = BigInt(candidate);
      if (matchCivil(candidate, candBi, tz, parts, node)) candidates.add(candidate);
    }
    return Array.from(candidates).sort((a, b) => a - b);
  };

  const dayOfWeekFromYMD = (year, month, day) => {
    const d = new Date(Date.UTC(year, month - 1, day));
    const dow = d.getUTCDay(); // 0=Sunday
    return dow === 0 ? 7 : dow;
  };

  const dayOfYearFromYMD = (year, month, day) => {
    let total = 0;
    for (let m = 1; m < month; m += 1) {
      total += Number(daysInMonthInt(BigInt(year), BigInt(m), null, null));
    }
    return total + day;
  };

  const weeksInISOYear = (year) => {
    const jan1Dow = dayOfWeekFromYMD(year, 1, 1); // 1..7
    if (jan1Dow === 4) return 53;
    if (jan1Dow === 3 && isLeapYearInt(BigInt(year))) return 53;
    return 52;
  };

  const isoWeekInfo = (year, month, day) => {
    const dow = dayOfWeekFromYMD(year, month, day);
    const doy = dayOfYearFromYMD(year, month, day);
    let week = Math.floor((doy - dow + 10) / 7);
    let weekYear = year;
    if (week < 1) {
      weekYear = year - 1;
      week = weeksInISOYear(weekYear);
    } else {
      const weeks = weeksInISOYear(year);
      if (week > weeks) {
        weekYear = year + 1;
        week = 1;
      }
    }
    return { weekYear, week };
  };

  const parseISO8601 = (node, s) => {
    if (typeof s !== "string") invalidISO(node);
    const len = s.length;
    const isDigit = (c) => c >= "0" && c <= "9";
    const read2 = (i) => {
      const a = s[i];
      const b = s[i + 1];
      if (!isDigit(a) || !isDigit(b)) return null;
      return (a.charCodeAt(0) - 48) * 10 + (b.charCodeAt(0) - 48);
    };
    const read4 = (i) => {
      const a = s[i];
      const b = s[i + 1];
      const c = s[i + 2];
      const d = s[i + 3];
      if (!isDigit(a) || !isDigit(b) || !isDigit(c) || !isDigit(d)) return null;
      return (a.charCodeAt(0) - 48) * 1000 + (b.charCodeAt(0) - 48) * 100 + (c.charCodeAt(0) - 48) * 10 + (d.charCodeAt(0) - 48);
    };
    if (len < 10) invalidISO(node);
    const year = read4(0);
    if (year === null || s[4] !== "-" || s[7] !== "-") invalidISO(node);
    const month = read2(5);
    const day = read2(8);
    if (month === null || day === null) invalidISO(node);
    let hour = 0;
    let minute = 0;
    let second = 0;
    let millisecond = 0;
    let i = 10;
    if (i === len) {
      // date only
    } else {
      if (s[i] !== "T") invalidISO(node);
      if (i + 8 >= len) invalidISO(node);
      const h = read2(i + 1);
      const m = read2(i + 4);
      const sec = read2(i + 7);
      if (h === null || m === null || sec === null) invalidISO(node);
      if (s[i + 3] !== ":" || s[i + 6] !== ":") invalidISO(node);
      hour = h;
      minute = m;
      second = sec;
      i += 9;
      if (i < len && s[i] === ".") {
        if (i + 3 >= len) invalidISO(node);
        const ms1 = s[i + 1];
        const ms2 = s[i + 2];
        const ms3 = s[i + 3];
        if (!isDigit(ms1) || !isDigit(ms2) || !isDigit(ms3)) invalidISO(node);
        millisecond = (ms1.charCodeAt(0) - 48) * 100 + (ms2.charCodeAt(0) - 48) * 10 + (ms3.charCodeAt(0) - 48);
        i += 4;
      }
    }
    let offsetMinutes = 0;
    if (i < len) {
      const signChar = s[i];
      if (signChar === "Z") {
        if (i + 1 !== len) invalidISO(node);
      } else if (signChar === "+" || signChar === "-") {
        if (i + 6 !== len) invalidISO(node);
        const oh = read2(i + 1);
        const om = read2(i + 4);
        if (oh === null || om === null || s[i + 3] !== ":") invalidISO(node);
        if (oh > 23 || om > 59) invalidISO(node);
        offsetMinutes = oh * 60 + om;
        if (signChar === "-") offsetMinutes = -offsetMinutes;
      } else {
        invalidISO(node);
      }
    }
    const parts = {
      year: BigInt(year),
      month: BigInt(month),
      day: BigInt(day),
      hour: BigInt(hour),
      minute: BigInt(minute),
      second: BigInt(second),
      millisecond: BigInt(millisecond),
    };
    const nums = validateCivilParts(node, parts, invalidISO);
    let epoch = Date.UTC(nums.yNum, nums.moNum - 1, nums.dNum, nums.hNum, nums.miNum, nums.sNum, nums.msNum);
    if (!Number.isFinite(epoch) || !Number.isSafeInteger(epoch)) invalidISO(node);
    epoch -= offsetMinutes * 60 * 1000;
    if (!Number.isFinite(epoch) || !Number.isSafeInteger(epoch)) invalidISO(node);
    return BigInt(epoch);
  };

  const formatISO8601 = (node, epochBi) => {
    const { num } = toEpochNumber(node, epochBi);
    const d = new Date(num);
    if (!Number.isFinite(d.getTime())) invalidDate(node, "invalid epoch");
    const year = d.getUTCFullYear();
    const month = d.getUTCMonth() + 1;
    const day = d.getUTCDate();
    const hour = d.getUTCHours();
    const minute = d.getUTCMinutes();
    const second = d.getUTCSeconds();
    const millisecond = d.getUTCMilliseconds();
    const pad2 = (v) => (v < 10 ? `0${v}` : String(v));
    const pad3 = (v) => (v < 10 ? `00${v}` : v < 100 ? `0${v}` : String(v));
    const padYear = (v) => {
      const sign = v < 0 ? "-" : "";
      const abs = Math.abs(v);
      const s = abs < 10 ? `000${abs}` : abs < 100 ? `00${abs}` : abs < 1000 ? `0${abs}` : String(abs);
      return sign + s;
    };
    return `${padYear(year)}-${pad2(month)}-${pad2(day)}T${pad2(hour)}:${pad2(minute)}:${pad2(second)}.${pad3(millisecond)}Z`;
  };

  const timeMod = makeModule("Time");
  timeMod.functions.set("nowEpochMillis", () => BigInt(Date.now()));
  timeMod.functions.set("nowMonotonicNanos", () => process.hrtime.bigint());
  timeMod.functions.set("sleepMillis", (ms, node) => {
    const v = toIntArg(node, ms);
    if (v <= 0n) return null;
    const n = Number(v);
    if (!Number.isFinite(n) || n < 0) invalidDate(node, "invalid sleep duration");
    const sab = new SharedArrayBuffer(4);
    const ia = new Int32Array(sab);
    Atomics.wait(ia, 0, 0, n);
    return null;
  });

  const timeCivilMod = makeModule("TimeCivil");
  timeCivilMod.constants.set("DST_EARLIER", 0n);
  timeCivilMod.constants.set("DST_LATER", 1n);
  timeCivilMod.constants.set("DST_ERROR", 2n);

  timeCivilMod.functions.set("fromEpochUTC", (epoch, node) => {
    const { bi, num } = toEpochNumber(node, epoch);
    const d = new Date(num);
    if (!Number.isFinite(d.getTime())) invalidDate(node, "invalid epoch");
    return makeCivilDateTime({
      year: d.getUTCFullYear(),
      month: d.getUTCMonth() + 1,
      day: d.getUTCDate(),
      hour: d.getUTCHours(),
      minute: d.getUTCMinutes(),
      second: d.getUTCSeconds(),
      millisecond: Number(((bi % 1000n) + 1000n) % 1000n),
    });
  });

  timeCivilMod.functions.set("toEpochUTC", (dt, node) => {
    const parts = readCivilDateTime(dt, node);
    const nums = validateCivilParts(node, parts, invalidDate);
    const epoch = Date.UTC(nums.yNum, nums.moNum - 1, nums.dNum, nums.hNum, nums.miNum, nums.sNum, nums.msNum);
    if (!Number.isFinite(epoch) || !Number.isSafeInteger(epoch)) invalidDate(node, "date out of range");
    return BigInt(epoch);
  });

  timeCivilMod.functions.set("fromEpoch", (epoch, tz, node) => {
    const { bi, num } = toEpochNumber(node, epoch);
    const parts = getCivilPartsFromEpoch(num, bi, tz, node);
    return makeCivilDateTime(parts);
  });

  timeCivilMod.functions.set("toEpoch", (dt, tz, strategy, node) => {
    const strat = toIntArg(node, strategy);
    if (strat !== 0n && strat !== 1n && strat !== 2n) {
      throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", "invalid DST strategy"));
    }
    const partsRaw = readCivilDateTime(dt, node);
    const nums = validateCivilParts(node, partsRaw, invalidDate);
    const parts = {
      year: nums.yNum,
      month: nums.moNum,
      day: nums.dNum,
      hour: nums.hNum,
      minute: nums.miNum,
      second: nums.sNum,
      millisecond: nums.msNum,
    };
    const candidates = findEpochCandidates(parts, tz, node);
    if (candidates.length === 0) dstNonExistent(node);
    if (candidates.length === 1) return BigInt(candidates[0]);
    if (strat === 2n) dstAmbiguous(node);
    return BigInt(strat === 0n ? candidates[0] : candidates[candidates.length - 1]);
  });

  timeCivilMod.functions.set("isDST", (epoch, tz, node) => {
    const { bi, num } = toEpochNumber(node, epoch);
    const off = offsetSecondsForEpoch(num, bi, tz, node);
    const std = (() => {
      const jan = Date.UTC(2024, 0, 15, 0, 0, 0, 0);
      const jul = Date.UTC(2024, 6, 15, 0, 0, 0, 0);
      const offJan = offsetSecondsForEpoch(jan, BigInt(jan), tz, node);
      const offJul = offsetSecondsForEpoch(jul, BigInt(jul), tz, node);
      return Math.min(offJan, offJul);
    })();
    return off !== std;
  });

  timeCivilMod.functions.set("offsetSeconds", (epoch, tz, node) => {
    const { bi, num } = toEpochNumber(node, epoch);
    return BigInt(offsetSecondsForEpoch(num, bi, tz, node));
  });

  timeCivilMod.functions.set("standardOffsetSeconds", (tz, node) => {
    const jan = Date.UTC(2024, 0, 15, 0, 0, 0, 0);
    const jul = Date.UTC(2024, 6, 15, 0, 0, 0, 0);
    const offJan = offsetSecondsForEpoch(jan, BigInt(jan), tz, node);
    const offJul = offsetSecondsForEpoch(jul, BigInt(jul), tz, node);
    return BigInt(Math.min(offJan, offJul));
  });

  timeCivilMod.functions.set("dayOfWeek", (epoch, tz, node) => {
    const { bi, num } = toEpochNumber(node, epoch);
    const parts = getCivilPartsFromEpoch(num, bi, tz, node);
    return BigInt(dayOfWeekFromYMD(parts.year, parts.month, parts.day));
  });

  timeCivilMod.functions.set("dayOfYear", (epoch, tz, node) => {
    const { bi, num } = toEpochNumber(node, epoch);
    const parts = getCivilPartsFromEpoch(num, bi, tz, node);
    return BigInt(dayOfYearFromYMD(parts.year, parts.month, parts.day));
  });

  timeCivilMod.functions.set("weekOfYearISO", (epoch, tz, node) => {
    const { bi, num } = toEpochNumber(node, epoch);
    const parts = getCivilPartsFromEpoch(num, bi, tz, node);
    const info = isoWeekInfo(parts.year, parts.month, parts.day);
    return BigInt(info.week);
  });

  timeCivilMod.functions.set("weekYearISO", (epoch, tz, node) => {
    const { bi, num } = toEpochNumber(node, epoch);
    const parts = getCivilPartsFromEpoch(num, bi, tz, node);
    const info = isoWeekInfo(parts.year, parts.month, parts.day);
    return BigInt(info.weekYear);
  });

  timeCivilMod.functions.set("isLeapYear", (year, node) => {
    const y = toIntArg(node, year);
    return isLeapYearInt(y);
  });

  timeCivilMod.functions.set("daysInMonth", (year, month, node) => {
    const y = toIntArg(node, year);
    const m = toIntArg(node, month);
    const dim = daysInMonthInt(y, m, node, (n, msg) =>
      timeError("InvalidDateException", n, msg || "invalid month")
    );
    return BigInt(dim);
  });

  timeCivilMod.functions.set("parseISO8601", (s, node) => parseISO8601(node, s));

  timeCivilMod.functions.set("formatISO8601", (epoch, node) => formatISO8601(node, epoch));

  const simpleMod = makeModule("test.simple");
  simpleMod.functions.set("add", (a, b) => BigInt(a) + BigInt(b));

  const utf8Mod = makeModule("test.utf8");
  utf8Mod.functions.set("roundtrip", (s) => {
    if (typeof s !== "string") {
      throw new RuntimeError(
        rdiag(file, null, "R1010", "RUNTIME_IO_ERROR", diagMsg("invalid argument", valueType(s), "string"))
      );
    }
    return s;
  });

  const throwMod = makeModule("test.throw");
  throwMod.functions.set("fail", () => {
    throw new RuntimeError(rdiag(file, null, "R1010", "RUNTIME_MODULE_ERROR", "native throw"));
  });

  makeModule("test.nosym");

  const sysRawEnv = new Map();
  const sysInvalidName = (node) => {
    throw makeIoException("InvalidEnvironmentNameException", file, node, "invalid environment name");
  };
  const sysValidateName = (name, node) => {
    if (typeof name !== "string" || name.length === 0 || name.includes("=")) sysInvalidName(node);
  };
  const sysHasOwnEnv = (name) => Object.prototype.hasOwnProperty.call(process.env, name);
  const sysDecodeUtf8 = (buf, node) => {
    try {
      const { TextDecoder } = require("util");
      const dec = new TextDecoder("utf-8", { fatal: true });
      return dec.decode(buf);
    } catch {
      throw makeIoException("EnvironmentAccessException", file, node, "invalid utf8");
    }
  };
  const sysMod = makeModule("Sys");
  const sysProtoEnv = () => (hooks && hooks.protoEnv ? hooks.protoEnv : buildPrototypeEnv({ decls: [] }));
  const sysMakeEvent = (stream, buf) => {
    const ev = clonePrototype(sysProtoEnv(), "ProcessEvent");
    ev.__fields.stream = BigInt(stream);
    const bytes = Array.from(buf || [], (b) => BigInt(b));
    ev.__fields.data = makeList(bytes);
    return ev;
  };
  const sysMakeResult = (exitCode, events) => {
    const res = clonePrototype(sysProtoEnv(), "ProcessResult");
    res.__fields.exitCode = BigInt(exitCode);
    res.__fields.events = events;
    return res;
  };
  sysMod.functions.set("hasEnv", (name, node) => {
    sysValidateName(name, node);
    if (sysRawEnv.has(name)) return true;
    return sysHasOwnEnv(name);
  });
  sysMod.functions.set("env", (name, node) => {
    sysValidateName(name, node);
    if (sysRawEnv.has(name)) {
      const raw = sysRawEnv.get(name);
      if (!Buffer.isBuffer(raw)) {
        throw makeIoException("EnvironmentAccessException", file, node, "invalid utf8");
      }
      return sysDecodeUtf8(raw, node);
    }
    if (!sysHasOwnEnv(name)) throw makeIoException("EnvironmentAccessException", file, node, "variable not found");
    const v = process.env[name];
    if (typeof v !== "string") throw makeIoException("EnvironmentAccessException", file, node, "variable not found");
    return v;
  });
  sysMod.functions.set("execute", (program, args, input, captureStdout, captureStderr, node) => {
    if (typeof program !== "string") throw makeIoException("InvalidArgumentException", file, node, "invalid program");
    if (program.length === 0) throw makeIoException("InvalidExecutableException", file, node, "invalid executable");
    if (!Array.isArray(args)) throw makeIoException("InvalidArgumentException", file, node, "invalid args");
    if (!Array.isArray(input)) throw makeIoException("InvalidArgumentException", file, node, "invalid input");
    if (typeof captureStdout !== "boolean" || typeof captureStderr !== "boolean") {
      throw makeIoException("InvalidArgumentException", file, node, "invalid capture flags");
    }

    const argv = [];
    for (const it of args) {
      if (typeof it !== "string") throw makeIoException("InvalidArgumentException", file, node, "invalid args");
      argv.push(it);
    }

    const bytes = [];
    for (const it of input) {
      const n = typeof it === "bigint" ? Number(it) : Number(it);
      if (!Number.isInteger(n) || n < 0 || n > 255) {
        throw makeIoException("InvalidArgumentException", file, node, "invalid input");
      }
      bytes.push(n);
    }

    const stdio = ["pipe", captureStdout ? "pipe" : "inherit", captureStderr ? "pipe" : "inherit"];
    const result = childProcess.spawnSync(program, argv, {
      input: Buffer.from(bytes),
      stdio,
      encoding: "buffer",
      maxBuffer: 1024 * 1024 * 16,
    });

    if (result.error) {
      const code = result.error.code;
      if (code === "ENOENT" || code === "ENOTDIR") {
        throw makeIoException("InvalidExecutableException", file, node, "invalid executable");
      }
      if (code === "EACCES") {
        throw makeIoException("ProcessPermissionException", file, node, "permission denied");
      }
      if (code === "EAGAIN") {
        throw makeIoException("ProcessCreationException", file, node, "fork failed");
      }
      throw makeIoException("ProcessExecutionException", file, node, "execution failed");
    }

    let exitCode = 0;
    if (typeof result.status === "number") exitCode = result.status;
    else if (result.signal) {
      const num = os.constants.signals[result.signal] || 0;
      exitCode = 128 + num;
    }

    const events = makeList([]);
    if (captureStdout && result.stdout && result.stdout.length > 0) {
      events.push(sysMakeEvent(1, result.stdout));
      bumpList(events);
    }
    if (captureStderr && result.stderr && result.stderr.length > 0) {
      events.push(sysMakeEvent(2, result.stderr));
      bumpList(events);
    }
    return sysMakeResult(exitCode, events);
  });

  const envMod = makeModule("test.env");
  envMod.functions.set("set", (name, value, node) => {
    if (typeof name !== "string" || typeof value !== "string") {
      throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_MODULE_ERROR", "invalid argument"));
    }
    sysRawEnv.delete(name);
    process.env[name] = value;
    return null;
  });
  envMod.functions.set("setInvalidUtf8", (name, node) => {
    if (typeof name !== "string") {
      throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_MODULE_ERROR", "invalid argument"));
    }
    const bad = Buffer.from([0xc3, 0x28]);
    sysRawEnv.set(name, bad);
    delete process.env[name];
    return null;
  });

  const rxMod = makeModule("RegExp");
  const rxThrow = (kind, node, msg) => {
    throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_MODULE_ERROR", `${kind}: ${msg}`));
  };
  const rxProtoEnv = () => (hooks && hooks.protoEnv ? hooks.protoEnv : buildPrototypeEnv({ decls: [] }));
  const rxMake = (pattern, flags, re) => {
    const obj = clonePrototype(rxProtoEnv(), "RegExp");
    obj.__fields._pattern = pattern;
    obj.__fields._flags = flags;
    obj.__fields._re = re;
    return obj;
  };
  const rxMatch = (ok, start, end, groups) => {
    const obj = clonePrototype(rxProtoEnv(), "RegExpMatch");
    obj.__fields.ok = !!ok;
    obj.__fields.start = BigInt(start);
    obj.__fields.end = BigInt(end);
    obj.__fields.groups = makeList(groups || []);
    return obj;
  };
  const rxEnsure = (r, node) => {
    if (!isObjectInstance(r) || r.__proto !== "RegExp" || !r.__fields || !(r.__fields._re instanceof RegExp)) {
      rxThrow("RegExpRange", node, "invalid RegExp handle");
    }
    return r;
  };
  const rxGlyphToCu = (s, g) => Array.from(s).slice(0, Number(g)).join("").length;
  const rxCuToGlyph = (s, cu) => Array.from(s.slice(0, cu)).length;
  const rxCheckRange = (s, start, node) => {
    const gl = Array.from(s).length;
    if (typeof start !== "bigint") rxThrow("RegExpRange", node, "start out of range");
    const n = Number(start);
    if (!Number.isInteger(n) || n < 0 || n > gl) rxThrow("RegExpRange", node, "start out of range");
    return n;
  };
  const rxCheckMax = (max, node, kind) => {
    if (typeof max !== "bigint") rxThrow("RegExpRange", node, `${kind} out of range`);
    const n = Number(max);
    if (!Number.isInteger(n)) rxThrow("RegExpRange", node, `${kind} out of range`);
    return n;
  };
  const rxForbidden = (p) =>
    /(?:\(\?=|\(\?!|\(\?<=|\(\?<!|\\[1-9])/.test(p);
  const rxTranslatePattern = (pattern) => {
    let out = "";
    let inClass = false;
    for (let i = 0; i < pattern.length; i += 1) {
      const ch = pattern[i];
      if (ch === "\\" && i + 1 < pattern.length) {
        const nx = pattern[i + 1];
        if (!inClass) {
          if (nx === "d") {
            out += "[0-9]";
            i += 1;
            continue;
          }
          if (nx === "D") {
            out += "[^0-9]";
            i += 1;
            continue;
          }
          if (nx === "w") {
            out += "[A-Za-z0-9_]";
            i += 1;
            continue;
          }
          if (nx === "W") {
            out += "[^A-Za-z0-9_]";
            i += 1;
            continue;
          }
          if (nx === "s") {
            out += "[ \\t\\r\\n\\f\\v]";
            i += 1;
            continue;
          }
          if (nx === "S") {
            out += "[^ \\t\\r\\n\\f\\v]";
            i += 1;
            continue;
          }
        }
        out += ch + nx;
        i += 1;
        continue;
      }
      if (!inClass && ch === "[") inClass = true;
      else if (inClass && ch === "]") inClass = false;
      out += ch;
    }
    return out;
  };
  const rxExpand = (replacement, groups) => {
    let out = "";
    for (let i = 0; i < replacement.length; i += 1) {
      const c = replacement[i];
      if (c !== "$") {
        out += c;
        continue;
      }
      const n1 = replacement[i + 1];
      if (n1 === "$") {
        out += "$";
        i += 1;
        continue;
      }
      if (!/[0-9]/.test(n1 || "")) {
        out += "$";
        continue;
      }
      let idx = Number(n1);
      if (/[0-9]/.test(replacement[i + 2] || "")) {
        idx = idx * 10 + Number(replacement[i + 2]);
        i += 1;
      }
      i += 1;
      out += groups[idx] ?? "";
    }
    return out;
  };
  const rxFind = (r, input, start, node) => {
    if (typeof input !== "string") rxThrow("RegExpRange", node, "input must be string");
    const sg = rxCheckRange(input, start, node);
    const startCu = rxGlyphToCu(input, sg);
    const sub = input.slice(startCu);
    const m = r.__fields._re.exec(sub);
    if (!m) return rxMatch(false, 0, 0, []);
    const mStartCu = startCu + m.index;
    const mEndCu = mStartCu + m[0].length;
    const gs = m.map((x) => (x === undefined ? "" : x));
    return rxMatch(true, rxCuToGlyph(input, mStartCu), rxCuToGlyph(input, mEndCu), gs);
  };
  rxMod.functions.set("compile", (pattern, flags, node) => {
    if (typeof pattern !== "string" || typeof flags !== "string") rxThrow("RegExpRange", node, "invalid arguments");
    if (rxForbidden(pattern)) rxThrow("RegExpSyntax", node, "forbidden metasyntax (backreference/lookaround)");
    const uniq = new Set(flags.split(""));
    let outFlags = "";
    if (uniq.has("i")) outFlags += "i";
    if (uniq.has("m")) outFlags += "m";
    if (uniq.has("s")) outFlags += "s";
    for (const f of uniq) {
      if (f !== "i" && f !== "m" && f !== "s") rxThrow("RegExpSyntax", node, "unsupported flag");
    }
    let re = null;
    try {
      re = new RegExp(rxTranslatePattern(pattern), outFlags);
    } catch (e) {
      rxThrow("RegExpSyntax", node, e && e.message ? String(e.message) : "invalid pattern");
    }
    return rxMake(pattern, outFlags, re);
  });
  rxMod.functions.set("test", (r, input, start, node) => {
    const rr = rxEnsure(r, node);
    return !!rxFind(rr, input, start, node).__fields.ok;
  });
  rxMod.functions.set("find", (r, input, start, node) => rxFind(rxEnsure(r, node), input, start, node));
  rxMod.functions.set("findAll", (r, input, start, max, node) => {
    const rr = rxEnsure(r, node);
    const m = rxCheckMax(max, node, "max");
    if (m < -1) rxThrow("RegExpRange", node, "max out of range");
    if (m === 0) return makeList([]);
    const out = makeList([]);
    let cur = rxCheckRange(input, start, node);
    while (cur <= Array.from(input).length) {
      const one = rxFind(rr, input, BigInt(cur), node);
      if (!one.__fields.ok) break;
      out.push(one);
      bumpList(out);
      if (m > 0 && out.length >= m) break;
      const a = Number(one.__fields.start);
      const b = Number(one.__fields.end);
      cur = b <= a ? b + 1 : b;
    }
    return out;
  });
  rxMod.functions.set("replaceFirst", (r, input, replacement, start, node) => {
    const rr = rxEnsure(r, node);
    if (typeof input !== "string" || typeof replacement !== "string") rxThrow("RegExpRange", node, "invalid arguments");
    const one = rxFind(rr, input, start, node);
    if (!one.__fields.ok) return input;
    const gs = one.__fields.groups;
    const s = Number(one.__fields.start);
    const e = Number(one.__fields.end);
    const sCu = rxGlyphToCu(input, s);
    const eCu = rxGlyphToCu(input, e);
    return input.slice(0, sCu) + rxExpand(replacement, gs) + input.slice(eCu);
  });
  rxMod.functions.set("replaceAll", (r, input, replacement, start, max, node) => {
    const rr = rxEnsure(r, node);
    if (typeof input !== "string" || typeof replacement !== "string") rxThrow("RegExpRange", node, "invalid arguments");
    const m = rxCheckMax(max, node, "max");
    if (m < -1) rxThrow("RegExpRange", node, "max out of range");
    if (m === 0) return input;
    let out = "";
    let cur = rxCheckRange(input, start, node);
    let prevCu = 0;
    let done = 0;
    prevCu = rxGlyphToCu(input, cur);
    out += input.slice(0, prevCu);
    while (cur <= Array.from(input).length) {
      const one = rxFind(rr, input, BigInt(cur), node);
      if (!one.__fields.ok) break;
      const s = Number(one.__fields.start);
      const e = Number(one.__fields.end);
      const sCu = rxGlyphToCu(input, s);
      const eCu = rxGlyphToCu(input, e);
      out += input.slice(prevCu, sCu);
      out += rxExpand(replacement, one.__fields.groups);
      done += 1;
      if (m > 0 && done >= m) {
        prevCu = eCu;
        break;
      }
      if (e <= s) {
        const next = e + 1;
        const nextCu = rxGlyphToCu(input, next);
        out += input.slice(eCu, nextCu);
        cur = next;
        prevCu = nextCu;
      } else {
        cur = e;
        prevCu = eCu;
      }
    }
    out += input.slice(prevCu);
    return out;
  });
  rxMod.functions.set("split", (r, input, start, maxParts, node) => {
    const rr = rxEnsure(r, node);
    if (typeof input !== "string") rxThrow("RegExpRange", node, "invalid arguments");
    const mp = rxCheckMax(maxParts, node, "maxParts");
    if (mp < -1) rxThrow("RegExpRange", node, "maxParts out of range");
    if (mp === 0) return makeList([]);
    const out = makeList([]);
    const limit = mp < 0 ? Number.MAX_SAFE_INTEGER : mp;
    let cur = rxCheckRange(input, start, node);
    let curCu = rxGlyphToCu(input, cur);
    if (mp === 1) {
      out.push(input.slice(curCu));
      bumpList(out);
      return out;
    }
    while (cur <= Array.from(input).length && out.length + 1 < limit) {
      const one = rxFind(rr, input, BigInt(cur), node);
      if (!one.__fields.ok) break;
      const s = Number(one.__fields.start);
      const e = Number(one.__fields.end);
      const sCu = rxGlyphToCu(input, s);
      const eCu = rxGlyphToCu(input, e);
      out.push(input.slice(curCu, sCu));
      bumpList(out);
      if (e <= s) {
        cur = e + 1;
        curCu = rxGlyphToCu(input, cur);
      } else {
        cur = e;
        curCu = eCu;
      }
    }
    out.push(input.slice(curCu));
    bumpList(out);
    return out;
  });
  rxMod.functions.set("pattern", (r, node) => {
    const rr = rxEnsure(r, node);
    return rr.__fields._pattern;
  });
  rxMod.functions.set("flags", (r, node) => {
    const rr = rxEnsure(r, node);
    return rr.__fields._flags;
  });

  loadModuleRegistry(); // ensures registry exists if needed

  const imports = ast.imports || [];
  for (const imp of imports) {
    const resolved = imp._resolved;
    if (resolved && resolved.kind === "proto") {
      if (imp.items && imp.items.length > 0) {
        for (const it of imp.items) {
          const local = it.alias || it.name;
          importedFunctions.set(local, { kind: "proto", proto: resolved.proto, name: it.name, node: it });
        }
      }
      continue;
    }
    const modName = imp.modulePath ? imp.modulePath.join(".") : "";
    if (!imp.items || imp.items.length === 0) {
      const alias = imp.alias || (imp.modulePath ? imp.modulePath[imp.modulePath.length - 1] : "");
      if (alias) namespaces.set(alias, modName);
      if (modName) makeModule(modName);
    } else {
      for (const it of imp.items) {
        const local = it.alias || it.name;
        importedFunctions.set(local, { module: modName, name: it.name, node: it });
      }
    }
  }

  return { modules, namespaces, importedFunctions };
}

function constValueToRuntime(c) {
  if (!c) return null;
  if (c.literalType === "int" || c.literalType === "byte") return parseIntLiteral(String(c.value));
  if (c.literalType === "float") return Number(c.value);
  if (c.literalType === "bool") return c.value === true || c.value === "true";
  if (c.literalType === "string") return String(c.value);
  if (c.literalType === "glyph") return new Glyph(Number(parseIntLiteral(String(c.value))));
  return c.value;
}

function buildGroupEnv(ast) {
  const typeToString = (t) => {
    if (!t) return "unknown";
    if (typeof t === "string") return t;
    if (t.kind === "PrimitiveType" || t.kind === "NamedType") return t.name;
    if (t.kind === "GenericType") return `${t.name}<${(t.args || []).map(typeToString).join(",")}>`;
    return "unknown";
  };
  const groups = new Map();
  for (const d of ast.decls || []) {
    if (d.kind !== "GroupDecl") continue;
    const members = new Map();
    const ordered = [];
    for (const m of d.members || []) {
      if (m && m.constValue) {
        const runtimeValue = constValueToRuntime(m.constValue);
        members.set(m.name, runtimeValue);
        ordered.push({
          name: m.name,
          literalType: m.constValue.literalType,
          value: String(m.constValue.value),
          runtimeValue,
        });
      }
    }
    const group = {
      name: d.name,
      baseType: typeToString(d.type),
      members: ordered,
      memberMap: members,
    };
    group.descriptor = {
      __group_desc: true,
      name: d.name,
      baseType: group.baseType,
      members: ordered,
    };
    groups.set(d.name, group);
  }
  return groups;
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

  let moduleEnv = null;
  const globalScope = new Scope(null);
  const callFunction = (fn, args) => {
    const scope = new Scope(globalScope);
    let argIndex = 0;
    if (fn.__protoOwner) {
      scope.define("self", args[0]);
      scope.define("__declProto", fn.__protoOwner);
      argIndex = 1;
    }
    for (let i = 0; i < fn.params.length; i += 1) {
      const p = fn.params[i];
      if (p.variadic) {
        const start = argIndex + i;
        const variadicValues = makeView(file, fn, args, start, Math.max(0, args.length - start), true);
        variadicValues.__type = variadicViewTypeName(p.type);
        scope.define(p.name, variadicValues);
        break;
      }
      scope.define(p.name, args[argIndex + i]);
    }
    try {
      execBlock(fn.body, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return null;
    } catch (e) {
      if (e instanceof ReturnSignal) return e.value;
      throw e;
    }
  };
  const groupEnv = buildGroupEnv(ast);
  moduleEnv = buildModuleEnv(ast, file, { protoEnv, functions, callFunction, groupEnv });
  moduleEnv.groups = groupEnv;
  for (const [alias, modName] of moduleEnv.namespaces.entries()) {
    const mod = moduleEnv.modules.get(modName);
    if (mod) globalScope.define(alias, mod.obj);
  }

  const argList = makeList(Array.isArray(argv) ? argv.slice() : []);
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
      if (stmt.init) {
        v = evalExpr(stmt.init, scope, functions, moduleEnv, protoEnv, file, callFunction);
      } else if (stmt.declaredType) {
        v = defaultValueForTypeNode(protoEnv, stmt.declaredType);
      }
      scope.define(stmt.name, v);
      return;
    }
    case "AssignStmt": {
      const op = stmt.op || "=";
      const ref = lvalueRef(stmt.target, scope, functions, moduleEnv, protoEnv, file, callFunction);
      if (!ref) {
        throw new RuntimeError(rdiag(file, stmt.target, "R1010", "RUNTIME_TYPE_ERROR", "invalid assignment target"));
      }
      const rhs = evalExpr(stmt.expr, scope, functions, moduleEnv, protoEnv, file, callFunction);
      if (op === "=") {
        ref.set(rhs);
        return;
      }
      const cur = ref.get();
      const binOp = op === "+=" ? "+" : op === "-=" ? "-" : op === "*=" ? "*" : op === "/=" ? "/" : null;
      if (!binOp) {
        throw new RuntimeError(rdiag(file, stmt, "R1010", "RUNTIME_TYPE_ERROR", "invalid assignment operator"));
      }
      const fake = { op: binOp, left: stmt.target, right: stmt.expr };
      const next = evalBinary(file, fake, cur, rhs);
      ref.set(next);
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
    case "IfStmt":
      execIf(stmt, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return;
    case "WhileStmt":
      execWhile(stmt, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return;
    case "DoWhileStmt":
      execDoWhile(stmt, scope, functions, moduleEnv, protoEnv, file, callFunction);
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
            if (exceptionMatches(ex, c.type, protoEnv)) {
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
      throw new BreakSignal();
    case "ContinueStmt":
      throw new ContinueSignal();
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
      try {
        execStmt(stmt.body, s, functions, moduleEnv, protoEnv, file, callFunction);
      } catch (e) {
        if (e instanceof ContinueSignal) {
          // continue to step
        } else if (e instanceof BreakSignal) {
          break;
        } else {
          throw e;
        }
      }
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
    ensureViewValid(file, stmt.iterExpr, seq);
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
    try {
      execStmt(stmt.body, s, functions, moduleEnv, protoEnv, file, callFunction);
    } catch (e) {
      if (e instanceof ContinueSignal) {
        continue;
      } else if (e instanceof BreakSignal) {
        break;
      }
      throw e;
    }
  }
}

function execSwitch(stmt, scope, functions, moduleEnv, protoEnv, file, callFunction) {
  const v = evalExpr(stmt.expr, scope, functions, moduleEnv, protoEnv, file, callFunction);
  for (const c of stmt.cases) {
    const cv = evalExpr(c.value, scope, functions, moduleEnv, protoEnv, file, callFunction);
    if (eqValue(v, cv)) {
      try {
        for (const st of c.stmts) execStmt(st, scope, functions, moduleEnv, protoEnv, file, callFunction);
      } catch (e) {
        if (e instanceof BreakSignal) return;
        throw e;
      }
      return;
    }
  }
  if (stmt.defaultCase) {
    try {
      for (const st of stmt.defaultCase.stmts) execStmt(st, scope, functions, moduleEnv, protoEnv, file, callFunction);
    } catch (e) {
      if (e instanceof BreakSignal) return;
      throw e;
    }
  }
}

function execWhile(stmt, scope, functions, moduleEnv, protoEnv, file, callFunction) {
  while (true) {
    const c = evalExpr(stmt.cond, scope, functions, moduleEnv, protoEnv, file, callFunction);
    if (!Boolean(c)) break;
    try {
      execStmt(stmt.body, scope, functions, moduleEnv, protoEnv, file, callFunction);
    } catch (e) {
      if (e instanceof ContinueSignal) continue;
      if (e instanceof BreakSignal) break;
      throw e;
    }
  }
}

function execDoWhile(stmt, scope, functions, moduleEnv, protoEnv, file, callFunction) {
  while (true) {
    try {
      execStmt(stmt.body, scope, functions, moduleEnv, protoEnv, file, callFunction);
    } catch (e) {
      if (e instanceof ContinueSignal) {
        // continue to condition
      } else if (e instanceof BreakSignal) {
        break;
      } else {
        throw e;
      }
    }
    const c = evalExpr(stmt.cond, scope, functions, moduleEnv, protoEnv, file, callFunction);
    if (!Boolean(c)) break;
  }
}

function execIf(stmt, scope, functions, moduleEnv, protoEnv, file, callFunction) {
  const cond = evalExpr(stmt.cond, scope, functions, moduleEnv, protoEnv, file, callFunction);
  if (cond === true) {
    execStmt(stmt.thenBranch, new Scope(scope), functions, moduleEnv, protoEnv, file, callFunction);
    return;
  }
  if (stmt.elseBranch) {
    execStmt(stmt.elseBranch, new Scope(scope), functions, moduleEnv, protoEnv, file, callFunction);
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
      if (scope.has(expr.name)) return scope.get(expr.name);
      if (moduleEnv && moduleEnv.groups && moduleEnv.groups.has(expr.name)) {
        const g = moduleEnv.groups.get(expr.name);
        return g && g.descriptor ? g.descriptor : { __group_desc: true, name: expr.name };
      }
      if (protoEnv && protoEnv.has(expr.name)) return { __proto_desc: true, name: expr.name };
      return scope.get(expr.name);
    case "SuperExpr":
      return { __super: true };
    case "UnaryExpr": {
      if (expr.op === "++" || expr.op === "--") {
        const ref = lvalueRef(expr.expr, scope, functions, moduleEnv, protoEnv, file, callFunction);
        if (!ref) {
          throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "invalid increment target"));
        }
        const v = ref.get();
        if (isGlyph(v)) {
          throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "invalid glyph operation"));
        }
        let next = v;
        if (typeof v === "bigint") {
          const d = expr.op === "++" ? 1n : -1n;
          next = checkIntRange(file, expr.expr, v + d);
        } else if (typeof v === "number") {
          const d = expr.op === "++" ? 1 : -1;
          next = v + d;
        } else {
          throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "invalid increment target"));
        }
        ref.set(next);
        return next;
      }
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
      return v;
    }
    case "BinaryExpr": {
      if (expr.op === "&&") {
        const l = evalExpr(expr.left, scope, functions, moduleEnv, protoEnv, file, callFunction);
        if (!Boolean(l)) return false;
        const r = evalExpr(expr.right, scope, functions, moduleEnv, protoEnv, file, callFunction);
        return Boolean(r);
      }
      if (expr.op === "||") {
        const l = evalExpr(expr.left, scope, functions, moduleEnv, protoEnv, file, callFunction);
        if (Boolean(l)) return true;
        const r = evalExpr(expr.right, scope, functions, moduleEnv, protoEnv, file, callFunction);
        return Boolean(r);
      }
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
    case "CastExpr": {
      const v = evalExpr(expr.expr, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return castNumeric(file, expr, expr.targetType, v);
    }
    case "IndexExpr": {
      const target = evalExpr(expr.target, scope, functions, moduleEnv, protoEnv, file, callFunction);
      const idx = evalExpr(expr.index, scope, functions, moduleEnv, protoEnv, file, callFunction);
      return indexGet(file, expr.target, expr.index, target, idx);
    }
    case "MemberExpr": {
      const target = evalExpr(expr.target, scope, functions, moduleEnv, protoEnv, file, callFunction);
      if (expr.target.kind === "Identifier" && moduleEnv && moduleEnv.groups && moduleEnv.groups.has(expr.target.name)) {
        const group = moduleEnv.groups.get(expr.target.name);
        const members = group && group.memberMap ? group.memberMap : null;
        if (members && members.has(expr.name)) return members.get(expr.name);
      }
      if (target && target.__group_desc && moduleEnv && moduleEnv.groups) {
        const group = moduleEnv.groups.get(target.name);
        const members = group && group.memberMap ? group.memberMap : null;
        if (members && members.has(expr.name)) return members.get(expr.name);
      }
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
        if (target.__fields && expr.name in target.__fields) return target.__fields[expr.name];
        throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "unknown field"));
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
      const ref = lvalueRef(expr.expr, scope, functions, moduleEnv, protoEnv, file, callFunction);
      if (!ref) {
        throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "invalid increment target"));
      }
      const v = ref.get();
      if (isGlyph(v)) {
        throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "invalid glyph operation"));
      }
      let next = v;
      if (typeof v === "bigint") {
        const d = expr.op === "++" ? 1n : -1n;
        next = checkIntRange(file, expr.expr, v + d);
      } else if (typeof v === "number") {
        const d = expr.op === "++" ? 1 : -1;
        next = v + d;
      } else {
        throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "invalid increment target"));
      }
      ref.set(next);
      return v;
    }
    case "ListLiteral":
      return makeList(expr.items.map((it) => evalExpr(it, scope, functions, moduleEnv, protoEnv, file, callFunction)));
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
  if (v && v.__group_desc) return `group:${v.name || "unknown"}`;
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

function lvalueRef(expr, scope, functions, moduleEnv, protoEnv, file, callFunction) {
  if (expr.kind === "Identifier") {
    return {
      get: () => scope.get(expr.name),
      set: (v) => scope.set(expr.name, v),
    };
  }
  if (expr.kind === "MemberExpr") {
    if (expr.target && expr.target.kind === "Identifier" && moduleEnv && moduleEnv.groups && moduleEnv.groups.has(expr.target.name)) {
      throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "group members are not assignable"));
    }
    const target = evalExpr(expr.target, scope, functions, moduleEnv, protoEnv, file, callFunction);
    if (target && target.__group_desc) {
      throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "group members are not assignable"));
    }
    if (isExceptionValue(target)) {
      if (expr.name === "file") return { get: () => target.file, set: (v) => { target.file = v; } };
      if (expr.name === "line") return { get: () => target.line, set: (v) => { target.line = v; } };
      if (expr.name === "column") return { get: () => target.column, set: (v) => { target.column = v; } };
      if (expr.name === "message") return { get: () => target.message, set: (v) => { target.message = v; } };
      if (expr.name === "cause") return { get: () => target.cause, set: (v) => { target.cause = v; } };
      if (expr.name === "code") return { get: () => target.code || "", set: (v) => { target.code = v; } };
      if (expr.name === "category") return { get: () => target.category || "", set: (v) => { target.category = v; } };
      if (target.__fields && expr.name in target.__fields) {
        return {
          get: () => target.__fields[expr.name],
          set: (v) => {
            if (target.__constFields && target.__constFields.has(expr.name)) {
              throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "cannot assign to const"));
            }
            target.__fields[expr.name] = v;
          },
        };
      }
      throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "unknown field"));
    }
    if (!isObjectInstance(target)) {
      throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "member assignment on non-object"));
    }
    if (!(expr.name in target.__fields)) {
      throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "unknown field"));
    }
    return {
      get: () => target.__fields[expr.name],
      set: (v) => {
        if (target.__constFields && target.__constFields.has(expr.name)) {
          throw new RuntimeError(rdiag(file, expr, "R1010", "RUNTIME_TYPE_ERROR", "cannot assign to const"));
        }
        target.__fields[expr.name] = v;
      },
    };
  }
  if (expr.kind === "IndexExpr") {
    const target = evalExpr(expr.target, scope, functions, moduleEnv, protoEnv, file, callFunction);
    const idx = evalExpr(expr.index, scope, functions, moduleEnv, protoEnv, file, callFunction);
    return {
      get: () => indexGet(file, expr.target, expr.index, target, idx),
      set: (v) => assignIndex(file, expr.target, expr.index, target, idx, v),
    };
  }
  return null;
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
      if (r === 0n) {
        throw new RuntimeError(
          rdiag(file, expr.right, "R1004", "RUNTIME_DIVIDE_BY_ZERO", diagMsg("division by zero", valueShort(r), "non-zero divisor"))
        );
      }
      return l / r;
    }
    if (op === "%") {
      if (r === 0n) {
        throw new RuntimeError(
          rdiag(file, expr.right, "R1004", "RUNTIME_DIVIDE_BY_ZERO", diagMsg("division by zero", valueShort(r), "non-zero divisor"))
        );
      }
      return l % r;
    }
    if (op === "<<") {
      if (r < 0n || r >= 64n) {
        throw new RuntimeError(
          rdiag(file, expr.right, "R1005", "RUNTIME_SHIFT_RANGE", diagMsg("invalid shift", valueShort(r), "0..63"))
        );
      }
      return checkIntRange(file, expr.left, l << r);
    }
    if (op === ">>") {
      if (r < 0n || r >= 64n) {
        throw new RuntimeError(
          rdiag(file, expr.right, "R1005", "RUNTIME_SHIFT_RANGE", diagMsg("invalid shift", valueShort(r), "0..63"))
        );
      }
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
      throw new RuntimeError(
        rdiag(file, targetNode, "R1002", "RUNTIME_INDEX_OOB", diagMsg("index out of bounds", valueShort(idx), "index within bounds"))
      );
    }
    return target[i];
  }
  if (isView(target)) {
    ensureViewValid(file, targetNode, target);
    const i = Number(idx);
    if (!Number.isInteger(i) || i < 0 || i >= target.len) {
      throw new RuntimeError(
        rdiag(file, targetNode, "R1002", "RUNTIME_INDEX_OOB", diagMsg("index out of bounds", valueShort(idx), "index within bounds"))
      );
    }
    if (Array.isArray(target.source)) return target.source[target.offset + i];
    if (typeof target.source === "string") return glyphAt(target.source, target.offset + i);
    return null;
  }
  if (typeof target === "string") {
    const glyphs = glyphStringsOf(target);
    const i = Number(idx);
    if (!Number.isInteger(i) || i < 0 || i >= glyphs.length) {
      throw new RuntimeError(
        rdiag(file, targetNode, "R1002", "RUNTIME_INDEX_OOB", diagMsg("index out of bounds", valueShort(idx), "index within bounds"))
      );
    }
    return glyphAt(target, i);
  }
  if (target instanceof Map) {
    const k = mapKey(idx);
    if (!target.has(k)) {
      throw new RuntimeError(
        rdiag(file, targetNode, "R1003", "RUNTIME_MISSING_KEY", diagMsg("missing key", valueShort(unmapKey(k)), "present key"))
      );
    }
    return target.get(k);
  }
  return null;
}

function assignIndex(file, targetNode, indexNode, target, idx, rhs) {
  if (Array.isArray(target)) {
    const i = Number(idx);
    if (!Number.isInteger(i) || i < 0 || i >= target.length) {
      throw new RuntimeError(
        rdiag(file, targetNode, "R1002", "RUNTIME_INDEX_OOB", diagMsg("index out of bounds", valueShort(idx), "index within bounds"))
      );
    }
    target[i] = rhs;
    return;
  }
  if (isView(target)) {
    ensureViewValid(file, targetNode, target);
    if (target.readonly) {
      throw new RuntimeError(rdiag(file, targetNode, "R1010", "RUNTIME_TYPE_ERROR", "cannot assign through view"));
    }
    const i = Number(idx);
    if (!Number.isInteger(i) || i < 0 || i >= target.len) {
      throw new RuntimeError(
        rdiag(file, targetNode, "R1002", "RUNTIME_INDEX_OOB", diagMsg("index out of bounds", valueShort(idx), "index within bounds"))
      );
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

function typeName(t) {
  if (!t) return "";
  if (typeof t === "string") return t;
  if (t.kind === "PrimitiveType" || t.kind === "NamedType") return t.name;
  return t.name || "";
}

function castNumeric(file, node, targetType, value) {
  const dst = typeName(targetType);
  if (dst === "byte") {
    if (typeof value === "bigint") return checkByteRange(file, node, value);
    if (typeof value === "number") {
      if (!Number.isFinite(value) || !Number.isInteger(value) || !Number.isSafeInteger(value)) {
        throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", "invalid float to byte"));
      }
      return checkByteRange(file, node, BigInt(value));
    }
    throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", "invalid cast to byte"));
  }
  if (dst === "int") {
    if (typeof value === "bigint") return value;
    if (typeof value === "number") {
      if (!Number.isFinite(value) || !Number.isInteger(value) || !Number.isSafeInteger(value)) {
        throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", "invalid float to int"));
      }
      return checkIntRange(file, node, BigInt(value));
    }
    throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", "invalid cast to int"));
  }
  if (dst === "float") {
    if (typeof value === "bigint") return Number(value);
    if (typeof value === "number") {
      if (!Number.isFinite(value)) {
        throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", "invalid float"));
      }
      return value;
    }
    throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", "invalid cast to float"));
  }
  throw new RuntimeError(rdiag(file, node, "R1010", "RUNTIME_TYPE_ERROR", "invalid numeric cast"));
}

function evalCall(expr, scope, functions, moduleEnv, protoEnv, file, callFunction) {
  // Function call by identifier.
  if (expr.callee.kind === "Identifier") {
    const fn = functions.get(expr.callee.name);
    if (!fn && moduleEnv && moduleEnv.importedFunctions.has(expr.callee.name)) {
      const info = moduleEnv.importedFunctions.get(expr.callee.name);
      if (info.kind === "proto") {
        const fn = functions.get(`${info.proto}.${info.name}`);
        if (fn) {
          const args = expr.args.map((a) => evalExpr(a, scope, functions, moduleEnv, protoEnv, file, callFunction));
          return callFunction(fn, args);
        }
        if (info.name === "clone") {
          if (!protoEnv || !protoEnv.has(info.proto)) {
            throw new RuntimeError(rdiag(file, info.node, "R1010", "RUNTIME_TYPE_ERROR", "unknown prototype"));
          }
          return objectCloneDefault(protoEnv, { __proto_desc: true, name: info.proto }, { scope, functions, moduleEnv, protoEnv, file, callFunction, node: info.node });
        }
        throw new RuntimeError(rdiag(file, info.node, "R1010", "RUNTIME_TYPE_ERROR", "missing method"));
      }
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

    if (protoEnv && expr._superCall) {
      const declProto = scope.has("__declProto") ? scope.get("__declProto") : null;
      if (!declProto) {
        throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "super outside method"));
      }
      const owner = protoEnv.get(declProto);
      if (!owner || !owner.parent) {
        throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "super has no parent"));
      }
      const info = resolveProtoMethodRuntime(protoEnv, owner.parent, m.name);
      if (!info) {
        throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "super method not found"));
      }
      const receiver = scope.get("self");
      const fn = functions.get(`${info.proto}.${m.name}`);
      if (fn) return callFunction(fn, [receiver, ...args]);
      if (m.name === "clone") {
        return objectCloneDefault(protoEnv, receiver, { scope, functions, moduleEnv, protoEnv, file, callFunction, node: m });
      }
      const mod = moduleEnv.modules.get(info.proto);
      const impl = mod && !mod.error ? mod.functions.get(m.name) : null;
      if (impl) return impl(receiver, ...args, m);
      throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "missing method"));
    }
    if (protoEnv && expr._protoStatic) {
      const info = resolveProtoMethodRuntime(protoEnv, expr._protoStatic, m.name);
      if (!info) throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "unknown prototype method"));
      const fn = functions.get(`${info.proto}.${m.name}`);
      const staticReceiver = target || { __proto_desc: true, name: expr._protoStatic };
      if (fn) {
        // Keep historical static-call semantics: explicit self for regular methods,
        // implicit receiver only for clone() (and arity-matched static helper cases).
        const implicitReceiver = m.name === "clone" || args.length === fn.params.length;
        const recvArg = implicitReceiver ? [staticReceiver] : [];
        return callFunction(fn, [...recvArg, ...args]);
      }
      if (m.name === "clone") {
        return objectCloneDefault(protoEnv, staticReceiver, { scope, functions, moduleEnv, protoEnv, file, callFunction, node: m });
      }
      const mod = moduleEnv.modules.get(info.proto);
      const impl = mod && !mod.error ? mod.functions.get(m.name) : null;
      if (impl) return impl(...args, m);
      throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "missing method"));
    }
    if (protoEnv && expr._protoInstance) {
      const dispatchProto = m.name === "clone" && isObjectInstance(target) ? target.__proto : expr._protoInstance;
      const info = resolveProtoMethodRuntime(protoEnv, dispatchProto, m.name);
      if (!info) throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "unknown prototype method"));
      const fn = functions.get(`${info.proto}.${m.name}`);
      if (fn) {
        return callFunction(fn, [target, ...args]);
      }
      if (m.name === "clone") {
        if (target && target.__fs_dir) {
          throw new RuntimeError(
            rdiag(file, m, "R1013", "RUNTIME_CLONE_NOT_SUPPORTED", "clone not supported for builtin handle Dir")
          );
        }
        if (target && target.__fs_walker) {
          throw new RuntimeError(
            rdiag(file, m, "R1013", "RUNTIME_CLONE_NOT_SUPPORTED", "clone not supported for builtin handle Walker")
          );
        }
        return objectCloneDefault(protoEnv, target, { scope, functions, moduleEnv, protoEnv, file, callFunction, node: m });
      }
      if (isObjectInstance(target) && target.__fields) {
        const proto = target.__proto || "";
        const getter = (name) => target.__fields[name];
        if (proto === "ProcessResult") {
          if (m.name === "exitCode") return getter("exitCode");
          if (m.name === "events") return getter("events");
        } else if (proto === "ProcessEvent") {
          if (m.name === "stream") return getter("stream");
          if (m.name === "data") return getter("data");
        } else if (proto === "RegExpMatch") {
          if (m.name === "ok") return getter("ok");
          if (m.name === "start") return getter("start");
          if (m.name === "end") return getter("end");
          if (m.name === "groups") return getter("groups");
        } else if (proto === "PathInfo") {
          if (m.name === "dirname") return getter("dirname");
          if (m.name === "basename") return getter("basename");
          if (m.name === "filename") return getter("filename");
          if (m.name === "extension") return getter("extension");
        } else if (proto === "PathEntry") {
          if (m.name === "path") return getter("path");
          if (m.name === "name") return getter("name");
          if (m.name === "depth") return getter("depth");
          if (m.name === "isDir") return getter("isDir");
          if (m.name === "isFile") return getter("isFile");
          if (m.name === "isSymlink") return getter("isSymlink");
        } else if (proto === "CivilDateTime") {
          const civilIntArg = () => {
            const v = args[0];
            if (typeof v !== "bigint") {
              throw new RuntimeError(
                rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", diagMsg("invalid argument", valueType(v), "int"))
              );
            }
            return v;
          };
          if (m.name === "year") return getter("year");
          if (m.name === "month") return getter("month");
          if (m.name === "day") return getter("day");
          if (m.name === "hour") return getter("hour");
          if (m.name === "minute") return getter("minute");
          if (m.name === "second") return getter("second");
          if (m.name === "millisecond") return getter("millisecond");
          if (m.name === "setYear") {
            target.__fields.year = civilIntArg();
            return null;
          }
          if (m.name === "setMonth") {
            target.__fields.month = civilIntArg();
            return null;
          }
          if (m.name === "setDay") {
            target.__fields.day = civilIntArg();
            return null;
          }
          if (m.name === "setHour") {
            target.__fields.hour = civilIntArg();
            return null;
          }
          if (m.name === "setMinute") {
            target.__fields.minute = civilIntArg();
            return null;
          }
          if (m.name === "setSecond") {
            target.__fields.second = civilIntArg();
            return null;
          }
          if (m.name === "setMillisecond") {
            target.__fields.millisecond = civilIntArg();
            return null;
          }
        }
      }
      const mod = moduleEnv.modules.get(info.proto);
      const impl = mod && !mod.error ? mod.functions.get(m.name) : null;
      if (impl) return impl(target, ...args, m);
      throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "missing method"));
    }

    const fsThrowEval = (type, message) => {
      throw makeIoException(type, file, m, message);
    };
    const fsInvalidPathCodes = new Set(["ENAMETOOLONG", "EINVAL", "ELOOP"]);
    const fsPermissionCodes = new Set(["EACCES", "EPERM"]);
    const fsThrowCommonEval = (code) => {
      if (code === "ENOENT") fsThrowEval("FileNotFoundException", "file not found");
      if (fsPermissionCodes.has(code)) fsThrowEval("PermissionDeniedException", "permission denied");
      if (code === "ENOTDIR" || fsInvalidPathCodes.has(code)) fsThrowEval("InvalidPathException", "invalid path");
      fsThrowEval("IOException", "io failed");
    };
    const fsThrowOpenDirEval = (code) => {
      if (code === "ENOENT") fsThrowEval("FileNotFoundException", "file not found");
      if (code === "ENOTDIR") fsThrowEval("NotADirectoryException", "not a directory");
      if (fsPermissionCodes.has(code)) fsThrowEval("PermissionDeniedException", "permission denied");
      if (fsInvalidPathCodes.has(code)) fsThrowEval("InvalidPathException", "invalid path");
      fsThrowEval("IOException", "io failed");
    };
    if (target && target.__fs_dir) {
      const dir = target;
      if (m.name === "clone") {
        expectArity(0, 0);
        throw new RuntimeError(
          rdiag(file, m, "R1013", "RUNTIME_CLONE_NOT_SUPPORTED", "clone not supported for builtin handle Dir")
        );
      }
      const fillNext = () => {
        if (dir.closed) fsThrowEval("IOException", "dir closed");
        if (dir.next !== null) return true;
        if (dir.done) return false;
        while (true) {
          let ent = null;
          try {
            ent = dir.handle.readSync();
          } catch (e) {
            const code = e && e.code ? String(e.code) : "";
            fsThrowEval("IOException", code ? code : "io failed");
          }
          if (!ent) {
            dir.done = true;
            return false;
          }
          if (ent.name === "." || ent.name === "..") continue;
          dir.next = ent.name;
          return true;
        }
      };
      if (m.name === "hasNext") return fillNext();
      if (m.name === "next") {
        if (!fillNext()) fsThrowEval("IOException", "no more entries");
        const name = dir.next;
        dir.next = null;
        return name;
      }
      if (m.name === "reset") {
        if (dir.closed) fsThrowEval("IOException", "dir closed");
        try {
          dir.handle.closeSync();
          dir.handle = fs.opendirSync(dir.path);
          dir.next = null;
          dir.done = false;
          dir.closed = false;
        } catch (e) {
          const code = e && e.code ? String(e.code) : "";
          fsThrowOpenDirEval(code);
        }
        return null;
      }
      if (m.name === "close") {
        if (!dir.closed) {
          try {
            dir.handle.closeSync();
          } catch {
          }
          dir.closed = true;
        }
        return null;
      }
    }
    if (target && target.__fs_walker) {
      const walker = target;
      if (m.name === "clone") {
        expectArity(0, 0);
        throw new RuntimeError(
          rdiag(file, m, "R1013", "RUNTIME_CLONE_NOT_SUPPORTED", "clone not supported for builtin handle Walker")
        );
      }
      const joinPath = (base, name) => (base.endsWith("/") ? base + name : `${base}/${name}`);
      const fillNext = () => {
        if (walker.closed) fsThrowEval("IOException", "walker closed");
        if (walker.next) return true;
        while (walker.stack.length > 0) {
          const frame = walker.stack[walker.stack.length - 1];
          let ent = null;
          try {
            ent = frame.dir.readSync();
          } catch (e) {
            const code = e && e.code ? String(e.code) : "";
            fsThrowEval("IOException", code ? code : "io failed");
          }
          if (!ent) {
            try { frame.dir.closeSync(); } catch {}
            walker.stack.pop();
            continue;
          }
          if (ent.name === "." || ent.name === "..") continue;
          const full = joinPath(frame.path, ent.name);
          let lst = null;
          try {
            lst = fs.lstatSync(full);
          } catch (e) {
            const code = e && e.code ? String(e.code) : "";
            fsThrowCommonEval(code);
          }
          let isSymlink = lst.isSymbolicLink();
          let isDir = lst.isDirectory();
          let isFile = lst.isFile();
          if (isSymlink && walker.followSymlinks) {
            try {
              const st = fs.statSync(full);
              isDir = st.isDirectory();
              isFile = st.isFile();
            } catch (e) {
              const code = e && e.code ? String(e.code) : "";
              if (code === "ENOENT") {
                isDir = false;
                isFile = false;
              } else {
                fsThrowCommonEval(code);
              }
            }
          } else if (isSymlink) {
            isDir = false;
            isFile = false;
          }
          const depth = frame.depth;
          if (isDir && (walker.maxDepth < 0 || depth < walker.maxDepth)) {
            try {
              const child = fs.opendirSync(full);
              walker.stack.push({ path: full, dir: child, depth: depth + 1 });
            } catch (e) {
              const code = e && e.code ? String(e.code) : "";
              fsThrowOpenDirEval(code);
            }
          }
          walker.next = { path: full, name: ent.name, depth, isDir, isFile, isSymlink };
          return true;
        }
        return false;
      };
      if (m.name === "hasNext") return fillNext();
      if (m.name === "next") {
        if (!fillNext()) fsThrowEval("IOException", "no more entries");
        const info = walker.next;
        walker.next = null;
        const obj = clonePrototype(protoEnv, "PathEntry");
        obj.__fields.path = info.path;
        obj.__fields.name = info.name;
        obj.__fields.depth = BigInt(info.depth);
        obj.__fields.isDir = !!info.isDir;
        obj.__fields.isFile = !!info.isFile;
        obj.__fields.isSymlink = !!info.isSymlink;
        return obj;
      }
      if (m.name === "close") {
        if (!walker.closed) {
          for (const frame of walker.stack) {
            try { frame.dir.closeSync(); } catch {}
          }
          walker.stack = [];
          walker.closed = true;
        }
        return null;
      }
    }

    if (isObjectInstance(target) && target.__fields) {
      const proto = target.__proto || "";
      const getter = (name) => target.__fields[name];
      if (proto === "ProcessResult") {
        if (m.name === "exitCode") return getter("exitCode");
        if (m.name === "events") return getter("events");
      } else if (proto === "ProcessEvent") {
        if (m.name === "stream") return getter("stream");
        if (m.name === "data") return getter("data");
      } else if (proto === "RegExpMatch") {
        if (m.name === "ok") return getter("ok");
        if (m.name === "start") return getter("start");
        if (m.name === "end") return getter("end");
        if (m.name === "groups") return getter("groups");
      } else if (proto === "PathInfo") {
        if (m.name === "dirname") return getter("dirname");
        if (m.name === "basename") return getter("basename");
        if (m.name === "filename") return getter("filename");
        if (m.name === "extension") return getter("extension");
      } else if (proto === "PathEntry") {
        if (m.name === "path") return getter("path");
        if (m.name === "name") return getter("name");
        if (m.name === "depth") return getter("depth");
        if (m.name === "isDir") return getter("isDir");
        if (m.name === "isFile") return getter("isFile");
        if (m.name === "isSymlink") return getter("isSymlink");
      } else if (proto === "CivilDateTime") {
        const civilIntArg = () => {
          const v = args[0];
          if (typeof v !== "bigint") {
            throw new RuntimeError(
              rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", diagMsg("invalid argument", valueType(v), "int"))
            );
          }
          return v;
        };
        if (m.name === "year") return getter("year");
        if (m.name === "month") return getter("month");
        if (m.name === "day") return getter("day");
        if (m.name === "hour") return getter("hour");
        if (m.name === "minute") return getter("minute");
        if (m.name === "second") return getter("second");
        if (m.name === "millisecond") return getter("millisecond");
        if (m.name === "setYear") {
          target.__fields.year = civilIntArg();
          return null;
        }
        if (m.name === "setMonth") {
          target.__fields.month = civilIntArg();
          return null;
        }
        if (m.name === "setDay") {
          target.__fields.day = civilIntArg();
          return null;
        }
        if (m.name === "setHour") {
          target.__fields.hour = civilIntArg();
          return null;
        }
        if (m.name === "setMinute") {
          target.__fields.minute = civilIntArg();
          return null;
        }
        if (m.name === "setSecond") {
          target.__fields.second = civilIntArg();
          return null;
        }
        if (m.name === "setMillisecond") {
          target.__fields.millisecond = civilIntArg();
          return null;
        }
      }
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
        if (t !== "bool") {
          throw new RuntimeError(
            rdiag(file, m, "R1010", "RUNTIME_JSON_ERROR", diagMsg("invalid JsonBool access", t || "unknown", "JsonBool"))
          );
        }
        return target.value;
      }
      if (m.name === "asNumber") {
        if (t !== "number") {
          throw new RuntimeError(
            rdiag(file, m, "R1010", "RUNTIME_JSON_ERROR", diagMsg("invalid JsonNumber access", t || "unknown", "JsonNumber"))
          );
        }
        return target.value;
      }
      if (m.name === "asString") {
        if (t !== "string") {
          throw new RuntimeError(
            rdiag(file, m, "R1010", "RUNTIME_JSON_ERROR", diagMsg("invalid JsonString access", t || "unknown", "JsonString"))
          );
        }
        return target.value;
      }
      if (m.name === "asArray") {
        if (t !== "array") {
          throw new RuntimeError(
            rdiag(file, m, "R1010", "RUNTIME_JSON_ERROR", diagMsg("invalid JsonArray access", t || "unknown", "JsonArray"))
          );
        }
        return jsonArraySnapshot(target.value);
      }
      if (m.name === "asObject") {
        if (t !== "object") {
          throw new RuntimeError(
            rdiag(file, m, "R1010", "RUNTIME_JSON_ERROR", diagMsg("invalid JsonObject access", t || "unknown", "JsonObject"))
          );
        }
        return jsonObjectSnapshot(target.value);
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
        return makeList(Array.from(bytes, (b) => BigInt(b)));
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
        throw new RuntimeError(
          rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", diagMsg("invalid view arity", `arity ${args.length}`, "arity 0 or 2"))
        );
      }
      if (Array.isArray(target)) {
        if (args.length !== 2) {
          throw new RuntimeError(
            rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", diagMsg("invalid view/slice arity", `arity ${args.length}`, "arity 2"))
          );
        }
        return makeView(file, m, target, args[0], args[1], m.name === "view");
      }
      if (isView(target)) {
        if (args.length !== 2) {
          throw new RuntimeError(
            rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", diagMsg("invalid view/slice arity", `arity ${args.length}`, "arity 2"))
          );
        }
        if (m.name === "view" && target.readonly) return makeView(file, m, target, args[0], args[1], true);
        if (m.name === "slice" && !target.readonly) return makeView(file, m, target, args[0], args[1], false);
        throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "invalid view/slice target"));
      }
    }

    const expectArity = (min, max, category = "RUNTIME_TYPE_ERROR") => {
      if (args.length < min || args.length > max) {
        const expected = min === max ? `arity ${min}` : `arity ${min}-${max}`;
        throw new RuntimeError(rdiag(file, m, "R1010", category, diagMsg("arity mismatch", `arity ${args.length}`, expected)));
      }
    };

    if (m.name === "length") {
      expectArity(0, 0);
      if (Array.isArray(target)) return BigInt(target.length);
      if (typeof target === "string") return BigInt(glyphStringsOf(target).length);
      if (target instanceof Map) return BigInt(target.size);
      if (isView(target)) {
        ensureViewValid(file, m, target);
        return BigInt(target.len);
      }
      return 0n;
    }
    if (m.name === "isEmpty") {
      expectArity(0, 0);
      if (Array.isArray(target)) return target.length === 0;
      if (typeof target === "string") return glyphStringsOf(target).length === 0;
      if (target instanceof Map) return target.size === 0;
      if (isView(target)) {
        ensureViewValid(file, m, target);
        return target.len === 0;
      }
      return false;
    }
    if (target instanceof Map) {
      if (m.name === "containsKey") {
        expectArity(1, 1);
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
      if (m.name === "remove") {
        expectArity(1, 1);
        if (target.size > 0) {
          const firstKey = target.keys().next().value;
          const expected = String(firstKey).split(":", 1)[0];
          const actual = mapKey(args[0]).split(":", 1)[0];
          if (expected !== actual) {
            throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "map key type mismatch"));
          }
        }
        return target.delete(mapKey(args[0]));
      }
      if (m.name === "keys") {
        expectArity(0, 0);
        return makeList(Array.from(target.keys()).map(unmapKey));
      }
      if (m.name === "values") {
        expectArity(0, 0);
        return makeList(Array.from(target.values()));
      }
    }

    if (typeof target === "string") {
      if (m.name === "toUpper") {
        expectArity(0, 0);
        return target.toUpperCase();
      }
      if (m.name === "toLower") {
        expectArity(0, 0);
        return target.toLowerCase();
      }
      if (m.name === "concat") {
        expectArity(1, 1);
        return String(target) + String(args[0]);
      }
      if (m.name === "subString") {
        expectArity(2, 2);
        const start = Number(args[0]);
        const length = Number(args[1]);
        const gs = glyphStringsOf(target);
        if (!Number.isInteger(start) || !Number.isInteger(length) || start < 0 || length < 0 || start + length > gs.length) {
          const got = `start=${start}, length=${length}`;
          throw new RuntimeError(rdiag(file, m, "R1002", "RUNTIME_INDEX_OOB", diagMsg("index out of bounds", got, "range within string")));
        }
        return gs.slice(start, start + length).join("");
      }
      if (m.name === "indexOf") {
        expectArity(1, 1);
        const needle = String(args[0]);
        return BigInt(indexOfGlyphs(target, needle));
      }
      if (m.name === "contains") {
        expectArity(1, 1);
        return indexOfGlyphs(target, String(args[0])) >= 0;
      }
      if (m.name === "lastIndexOf") {
        expectArity(1, 1);
        return BigInt(lastIndexOfGlyphs(target, String(args[0])));
      }
      if (m.name === "startsWith") {
        expectArity(1, 1);
        return target.startsWith(String(args[0]));
      }
      if (m.name === "endsWith") {
        expectArity(1, 1);
        return target.endsWith(String(args[0]));
      }
      if (m.name === "split") {
        expectArity(1, 1);
        const sep = String(args[0]);
        if (sep === "") return makeList(glyphStringsOf(target));
        return makeList(target.split(sep));
      }
      if (m.name === "trim") {
        expectArity(0, 0);
        return trimAscii(target, "both");
      }
      if (m.name === "trimStart") {
        expectArity(0, 0);
        return trimAscii(target, "start");
      }
      if (m.name === "trimEnd") {
        expectArity(0, 0);
        return trimAscii(target, "end");
      }
      if (m.name === "replace") {
        expectArity(2, 2);
        return target.replace(String(args[0]), String(args[1]));
      }
      if (m.name === "replaceAll") {
        expectArity(2, 2);
        const out = replaceAllGlyphs(target, String(args[0]), String(args[1]));
        if (out === null) {
          throw new RuntimeError(rdiag(file, m, "R1009", "RUNTIME_INVALID_ARGUMENT", diagMsg("invalid argument", "oldValue=\"\"", "non-empty oldValue")));
        }
        return out;
      }
      if (m.name === "glyphAt") {
        expectArity(1, 1);
        const idx = Number(args[0]);
        const gs = glyphStringsOf(target);
        if (!Number.isInteger(idx) || idx < 0 || idx >= gs.length) {
          const got = `index=${idx}`;
          throw new RuntimeError(rdiag(file, m, "R1002", "RUNTIME_INDEX_OOB", diagMsg("index out of bounds", got, "index within string")));
        }
        return new Glyph(gs[idx].codePointAt(0));
      }
      if (m.name === "repeat") {
        expectArity(1, 1);
        const count = Number(args[0]);
        if (!Number.isInteger(count) || count < 0) {
          throw new RuntimeError(rdiag(file, m, "R1009", "RUNTIME_INVALID_ARGUMENT", diagMsg("invalid argument", `count=${count}`, "count >= 0")));
        }
        if (count === 0) return "";
        return target.repeat(count);
      }
      if (m.name === "padStart" || m.name === "padEnd") {
        expectArity(2, 2);
        const targetLength = Number(args[0]);
        const pad = String(args[1]);
        const gs = glyphStringsOf(target);
        if (!Number.isInteger(targetLength)) {
          throw new RuntimeError(rdiag(file, m, "R1009", "RUNTIME_INVALID_ARGUMENT", diagMsg("invalid argument", `targetLength=${targetLength}`, "integer targetLength")));
        }
        if (targetLength <= gs.length) return target;
        const padGlyphs = glyphStringsOf(pad);
        if (padGlyphs.length === 0) {
          throw new RuntimeError(rdiag(file, m, "R1009", "RUNTIME_INVALID_ARGUMENT", diagMsg("invalid argument", "pad=\"\"", "non-empty pad when padding is required")));
        }
        const need = targetLength - gs.length;
        const fill = [];
        while (fill.length < need) {
          for (const g of padGlyphs) {
            if (fill.length >= need) break;
            fill.push(g);
          }
        }
        const filler = fill.join("");
        return m.name === "padStart" ? `${filler}${target}` : `${target}${filler}`;
      }
      if (m.name === "toUtf8Bytes") {
        expectArity(0, 0);
        const enc = new TextEncoder();
        const bytes = enc.encode(target);
        return makeList(Array.from(bytes, (b) => BigInt(b)));
      }
    }

    if (Array.isArray(target) && m.name === "toUtf8String") {
      expectArity(0, 0);
      const bytes = [];
      for (const v of target) {
        const n = typeof v === "bigint" ? Number(v) : Number(v);
        if (!Number.isInteger(n) || n < 0 || n > 255) {
          throw new RuntimeError(rdiag(file, m, "R1007", "RUNTIME_INVALID_UTF8", diagMsg("invalid UTF-8 sequence", "byte stream", "valid UTF-8")));
        }
        bytes.push(n);
      }
      try {
        const dec = new TextDecoder("utf-8", { fatal: true });
        return dec.decode(Uint8Array.from(bytes));
      } catch {
        throw new RuntimeError(rdiag(file, m, "R1007", "RUNTIME_INVALID_UTF8", diagMsg("invalid UTF-8 sequence", "byte stream", "valid UTF-8")));
      }
    }
    if (Array.isArray(target)) {
      if (m.name === "push") {
        expectArity(1, 1);
        target.push(args[0]);
        bumpList(target);
        return BigInt(target.length);
      }
      if (m.name === "contains") {
        expectArity(1, 1);
        for (const v of target) {
          if (eqValue(v, args[0])) return true;
        }
        return false;
      }
      if (m.name === "reverse") {
        expectArity(0, 0);
        for (let i = 0, j = target.length - 1; i < j; i += 1, j -= 1) {
          const tmp = target[i];
          target[i] = target[j];
          target[j] = tmp;
        }
        return BigInt(target.length);
      }
      if (m.name === "sort") {
        expectArity(0, 0);
        if (target.length === 0) return BigInt(0);
        const elemType = expr._listSortElemType;
        if (!elemType) {
          throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "list element not comparable"));
        }
        let cmp = null;
        if (elemType === "int" || elemType === "byte") {
          for (const v of target) {
            if (typeof v !== "bigint") {
              throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "list element not comparable"));
            }
          }
          cmp = (a, b) => (a < b ? -1 : a > b ? 1 : 0);
        } else if (elemType === "float") {
          for (const v of target) {
            if (typeof v !== "number") {
              throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "list element not comparable"));
            }
          }
          cmp = (a, b) => {
            const na = Number.isNaN(a);
            const nb = Number.isNaN(b);
            if (na && nb) return 0;
            if (na) return 1;
            if (nb) return -1;
            return a < b ? -1 : a > b ? 1 : 0;
          };
        } else if (elemType === "string") {
          for (const v of target) {
            if (typeof v !== "string") {
              throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "list element not comparable"));
            }
          }
          const enc = new TextEncoder();
          const cache = new Map();
          const bytesOf = (s) => {
            let b = cache.get(s);
            if (!b) {
              b = enc.encode(s);
              cache.set(s, b);
            }
            return b;
          };
          cmp = (a, b) => compareUtf8Bytes(bytesOf(a), bytesOf(b));
        } else if (protoEnv && typeof elemType === "string" && protoEnv.has(elemType)) {
          const info = resolveProtoMethodRuntime(protoEnv, elemType, "compareTo");
          if (!info) {
            throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "list element not comparable"));
          }
          const fn = functions.get(`${info.proto}.compareTo`);
          if (!fn) {
            throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "list element not comparable"));
          }
          cmp = (a, b) => {
            if (!isObjectInstance(a) || !isObjectInstance(b)) {
              throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "list element not comparable"));
            }
            const r = callFunction(fn, [a, b]);
            if (typeof r !== "bigint") {
              throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "compareTo must return int"));
            }
            return r < 0n ? -1 : r > 0n ? 1 : 0;
          };
        } else {
          throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "list element not comparable"));
        }
        if (target.length > 1) stableSortInPlace(target, cmp);
        return BigInt(target.length);
      }
    }
    if (Array.isArray(target) && (m.name === "join" || m.name === "concat")) {
      if (m.name === "join") expectArity(1, 1);
      if (m.name === "concat") expectArity(0, 0);
      for (const v of target) {
        if (typeof v !== "string") {
          throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "join expects list<string>"));
        }
      }
      if (m.name === "concat") return target.join("");
      const sep = args[0];
      if (typeof sep !== "string") {
        throw new RuntimeError(rdiag(file, m, "R1010", "RUNTIME_TYPE_ERROR", "join separator must be string"));
      }
      return target.join(sep);
    }

    if (target instanceof TextFile || target instanceof BinaryFile) {
      if (m.name === "clone") {
        expectArity(0, 0, "RUNTIME_IO_ERROR");
        const handle = target instanceof BinaryFile ? "BinaryFile" : "TextFile";
        throw new RuntimeError(
          rdiag(file, m, "R1013", "RUNTIME_CLONE_NOT_SUPPORTED", `clone not supported for builtin handle ${handle}`)
        );
      }
      if (m.name === "close") {
        expectArity(0, 0, "RUNTIME_IO_ERROR");
        if (target.isStd) {
          throwIoException("StandardStreamCloseException", file, m, "cannot close standard stream");
        }
        if (!target.closed) {
          fs.closeSync(target.fd);
          target.closed = true;
        }
        return null;
      }
      if (target.closed) {
        throwIoException("FileClosedException", file, m, "file is closed");
      }
      const isBinary = target instanceof BinaryFile;
      const canRead = (target.flags & PS_FILE_READ) !== 0;
      const canWrite = (target.flags & (PS_FILE_WRITE | PS_FILE_APPEND)) !== 0;
      if (m.name === "read") {
        expectArity(1, 1, "RUNTIME_IO_ERROR");
        if (!canRead) {
          throwIoException("ReadFailureException", file, m, "file not readable");
        }
        const size = Number(args[0]);
        if (!Number.isInteger(size) || size <= 0) {
          throwIoException("InvalidArgumentException", file, m, "invalid read size");
        }
        if (isBinary) {
          const buf = Buffer.alloc(size);
          let n = 0;
          try {
            n = fs.readSync(target.fd, buf, 0, size, target.isStd ? null : target.posBytes);
          } catch {
            throwIoException("ReadFailureException", file, m, "read failed");
          }
          if (n === 0) return makeList([]);
          if (!target.isStd) target.posBytes += n;
          return makeList(Array.from(buf.slice(0, n)).map((b) => BigInt(b)));
        }
        const { text, newPos } = readTextGlyphs(target, m, size);
        target.posBytes = newPos;
        return text;
      }
      if (m.name === "write") {
        expectArity(1, 1, "RUNTIME_IO_ERROR");
        if (!canWrite) {
          throwIoException("WriteFailureException", file, m, "file not writable");
        }
        const v = args[0];
        if (!isBinary) {
          if (typeof v !== "string") {
            throwIoException("InvalidArgumentException", file, m, "invalid write value");
          }
          if (v.length > 0) {
            const buf = Buffer.from(v, "utf8");
            const startPos = target.posBytes;
            writeAllAtomic(target.fd, buf, target.isStd ? null : startPos, file, m);
            if (!target.isStd) target.posBytes = startPos + buf.length;
          }
          return null;
        }
        if (!Array.isArray(v)) {
          throwIoException("InvalidArgumentException", file, m, "invalid write value");
        }
        const bytes = [];
        for (const it of v) {
          const n = typeof it === "bigint" ? Number(it) : Number(it);
          if (!Number.isInteger(n) || n < 0 || n > 255) {
            throwIoException("InvalidArgumentException", file, m, "invalid byte value");
          }
          bytes.push(n);
        }
        if (bytes.length > 0) {
          const buf = Buffer.from(bytes);
          const startPos = target.posBytes;
          writeAllAtomic(target.fd, buf, target.isStd ? null : startPos, file, m);
          if (!target.isStd) target.posBytes = startPos + buf.length;
        }
        return null;
      }
      if (m.name === "tell") {
        expectArity(0, 0, "RUNTIME_IO_ERROR");
        if (isBinary) return BigInt(target.posBytes);
        return BigInt(glyphIndexAtBytePos(target, m));
      }
      if (m.name === "size") {
        expectArity(0, 0, "RUNTIME_IO_ERROR");
        if (isBinary) {
          try {
            return BigInt(fs.fstatSync(target.fd).size);
          } catch {
            throwIoException("ReadFailureException", file, m, "size failed");
          }
        }
        return BigInt(glyphCountTotal(target, m));
      }
      if (m.name === "seek") {
        expectArity(1, 1, "RUNTIME_IO_ERROR");
        const pos = Number(args[0]);
        if (!Number.isInteger(pos) || pos < 0) {
          throwIoException("InvalidArgumentException", file, m, "invalid seek position");
        }
        if (isBinary) {
          let size = 0;
          try {
            size = fs.fstatSync(target.fd).size;
          } catch {
            throwIoException("ReadFailureException", file, m, "seek failed");
          }
          if (pos > size) {
            throwIoException("InvalidArgumentException", file, m, "seek out of range");
          }
          target.posBytes = pos;
          return null;
        }
        target.posBytes = byteOffsetForGlyph(target, m, pos);
        return null;
      }
      if (m.name === "name") {
        expectArity(0, 0, "RUNTIME_IO_ERROR");
        return target.path || "";
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
      expectArity(0, 0);
      if (target.length === 0) {
        throw new RuntimeError(rdiag(file, m.target, "R1006", "RUNTIME_EMPTY_POP", "pop on empty list"));
      }
      const v = target.pop();
      bumpList(target);
      return v;
    }
    if (m.name === "removeLast" && Array.isArray(target)) {
      expectArity(0, 0);
      if (target.length === 0) {
        throw new RuntimeError(rdiag(file, m.target, "R1006", "RUNTIME_EMPTY_POP", "pop on empty list"));
      }
      target.pop();
      bumpList(target);
      return null;
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
