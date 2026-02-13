"use strict";

const fs = require("fs");
const path = require("path");

function typeToString(t) {
  if (!t) return "unknown";
  if (typeof t === "string") return t;
  if (t.kind === "PrimitiveType" || t.kind === "NamedType") return t.name;
  if (t.kind === "GenericType") return `${t.name}<${t.args.map(typeToString).join(",")}>`;
  if (t.kind === "IRType") return t.name;
  return "unknown";
}

function splitTypeArgs(inner) {
  const parts = [];
  let depth = 0;
  let start = 0;
  for (let i = 0; i < inner.length; i += 1) {
    const ch = inner[i];
    if (ch === "<") depth += 1;
    else if (ch === ">") depth -= 1;
    else if (ch === "," && depth === 0) {
      parts.push(inner.slice(start, i).trim());
      start = i + 1;
    }
  }
  parts.push(inner.slice(start).trim());
  return parts.filter((p) => p.length > 0);
}

function parseTypeName(name) {
  if (!name || typeof name !== "string") return { kind: "PrimitiveType", name: "unknown" };
  if (["int", "float", "bool", "byte", "glyph", "string", "void"].includes(name)) {
    return { kind: "PrimitiveType", name };
  }
  const m = /^([a-zA-Z_][a-zA-Z0-9_]*)<(.*)>$/.exec(name);
  if (m) {
    const base = m[1];
    const args = splitTypeArgs(m[2]).map((p) => parseTypeName(p));
    return { kind: "GenericType", name: base, args };
  }
  return { kind: "NamedType", name };
}

function lowerType(t) {
  if (!t) return { kind: "IRType", name: "void" };
  if (t.kind === "PrimitiveType" || t.kind === "NamedType") return { kind: "IRType", name: t.name };
  if (t.kind === "GenericType") {
    const name = t.name;
    if (name === "slice" || name === "view") {
      return {
        kind: "IRType",
        name: `${name}<${lowerType(t.args[0]).name}>`,
        repr: "(ptr,len)",
      };
    }
    if (name === "list") {
      return {
        kind: "IRType",
        name: `list<${lowerType(t.args[0]).name}>`,
        repr: "(ptr,len,cap)",
      };
    }
    if (name === "map") {
      return {
        kind: "IRType",
        name: `map<${lowerType(t.args[0]).name},${lowerType(t.args[1]).name}>`,
        repr: "(hash_table,order)",
      };
    }
  }
  return { kind: "IRType", name: typeToString(t) };
}

function isIntLike(t) {
  const n = typeToString(t);
  return n === "int" || n === "byte";
}

function intLiteralToBigInt(raw) {
  if (raw === null || raw === undefined) return null;
  const s = String(raw);
  if (/^0[xX]/.test(s)) return BigInt(s);
  if (/^0[bB]/.test(s)) return BigInt(s);
  if (/^0[0-7]+$/.test(s)) return BigInt(`0o${s.slice(1)}`);
  return BigInt(s);
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

function buildImportMap(ast) {
  const imported = new Map();
  const namespaces = new Map();
  const moduleConsts = new Map(); // module -> Map(name -> value)
  const moduleReturns = new Map(); // module -> Map(name -> retType)
  const importedReturns = new Map(); // local symbol -> retType
  const imports = ast.imports || [];
  if (imports.length === 0) return { imported, namespaces, moduleConsts, moduleReturns, importedReturns };
  const reg = loadModuleRegistry();
  if (!reg || !Array.isArray(reg.modules)) return { imported, namespaces, moduleConsts, moduleReturns, importedReturns };
  const modules = new Map();
  for (const m of reg.modules) {
    if (!m || typeof m.name !== "string") continue;
    const fns = new Set();
    const retMap = new Map();
    for (const f of m.functions || []) {
      if (f && typeof f.name === "string") {
        fns.add(f.name);
        if (typeof f.ret === "string") retMap.set(f.name, f.ret);
      }
    }
    modules.set(m.name, fns);
    moduleReturns.set(m.name, retMap);
    const consts = new Map();
    for (const c of m.constants || []) {
      if (!c || typeof c.name !== "string" || typeof c.type !== "string") continue;
      if (c.type === "float") {
        if (typeof c.value !== "string" && typeof c.value !== "number") continue;
        consts.set(c.name, { type: "float", value: String(c.value) });
      } else if (c.type === "int") {
        if (typeof c.value !== "string" && typeof c.value !== "number") continue;
        consts.set(c.name, { type: "int", value: String(c.value) });
      } else if (c.type === "string") {
        if (typeof c.value !== "string") continue;
        consts.set(c.name, { type: "string", value: c.value });
      } else if (c.type === "TextFile" || c.type === "BinaryFile") {
        if (typeof c.value !== "string") continue;
        consts.set(c.name, { type: c.type, value: c.value });
      } else if (c.type === "eof") {
        consts.set(c.name, { type: "eof", value: "" });
      }
    }
    moduleConsts.set(m.name, consts);
  }
  for (const imp of imports) {
    if (imp._resolved && imp._resolved.kind === "proto") {
      const proto = imp._resolved.proto;
      if (imp.items && imp.items.length > 0) {
        for (const it of imp.items) {
          const local = it.alias || it.name;
          imported.set(local, `${proto}.${it.name}`);
        }
      } else {
        const alias = imp.alias || (imp.modulePath ? imp.modulePath[imp.modulePath.length - 1] : proto);
        namespaces.set(alias, proto);
      }
      continue;
    }
    const mod = imp.modulePath.join(".");
    const mset = modules.get(mod);
    if (!mset) continue;
    if (imp.items && imp.items.length > 0) {
      for (const it of imp.items) {
        if (!mset.has(it.name)) continue;
        const local = it.alias || it.name;
        imported.set(local, `${mod}.${it.name}`);
        const modRets = moduleReturns.get(mod);
        if (modRets && modRets.has(it.name)) importedReturns.set(local, modRets.get(it.name));
      }
    } else {
      const alias = imp.alias || imp.modulePath[imp.modulePath.length - 1];
      namespaces.set(alias, mod);
    }
  }
  return { imported, namespaces, moduleConsts, moduleReturns, importedReturns };
}

class Scope {
  constructor(parent = null) {
    this.parent = parent;
    this.vars = new Map();
  }
  define(name, t, irName) {
    this.vars.set(name, { type: t, irName });
  }
  get(name) {
    if (this.vars.has(name)) return this.vars.get(name);
    if (this.parent) return this.parent.get(name);
    return null;
  }
}

class IRBuilder {
  constructor(ast) {
    this.ast = ast;
    this.tempId = 0;
    this.blockId = 0;
    this.varId = 0;
    this.breakStack = [];
    this.continueStack = [];
    this.functions = new Map();
    this.prototypes = new Map();
    this.groupConsts = new Map();
    const imports = buildImportMap(ast);
    this.importedSymbols = imports.imported;
    this.importNamespaces = imports.namespaces;
    this.moduleConsts = imports.moduleConsts;
    this.moduleReturns = imports.moduleReturns;
    this.importedReturns = imports.importedReturns;
    this.collectGroupConsts();
  }

  collectGroupConsts() {
    for (const d of this.ast.decls || []) {
      if (d.kind !== "GroupDecl") continue;
      const members = new Map();
      for (const m of d.members || []) {
        if (!m || !m.constValue) continue;
        members.set(m.name, { literalType: m.constValue.literalType, value: m.constValue.value });
      }
      this.groupConsts.set(d.name, { members, baseType: typeToString(d.type) });
    }
  }

  buildGroups() {
    const groups = [];
    for (const d of this.ast.decls || []) {
      if (d.kind !== "GroupDecl") continue;
      const members = [];
      for (const m of d.members || []) {
        if (!m || !m.constValue) continue;
        members.push({
          name: m.name,
          literalType: m.constValue.literalType,
          value: String(m.constValue.value),
        });
      }
      groups.push({
        name: d.name,
        baseType: { kind: "IRType", name: typeToString(d.type) },
        members,
      });
    }
    return groups;
  }

  inferFileTypeFromIoOpen(expr) {
    if (!expr || expr.kind !== "CallExpr") return null;
    if (!expr.callee || expr.callee.kind !== "MemberExpr") return null;
    const callee = expr.callee;
    if (!callee.target || callee.target.kind !== "Identifier") return null;
    if (callee.target.name !== "Io") return null;
    if (callee.name === "openText") return "TextFile";
    if (callee.name === "openBinary") return "BinaryFile";
    return null;
  }

  nextTemp() {
    this.tempId += 1;
    return `%t${this.tempId}`;
  }

  nextVar(name) {
    this.varId += 1;
    return `${name}$${this.varId}`;
  }

  nextBlock(prefix = "b") {
    this.blockId += 1;
    return `${prefix}${this.blockId}`;
  }

  emit(block, instr) {
    block.instrs.push(instr);
  }

  pushLoopTargets(breakTarget, continueTarget) {
    this.breakStack.push(breakTarget);
    this.continueStack.push(continueTarget);
  }

  popLoopTargets() {
    this.breakStack.pop();
    this.continueStack.pop();
  }

  pushBreakTarget(breakTarget) {
    this.breakStack.push(breakTarget);
  }

  popBreakTarget() {
    this.breakStack.pop();
  }

  build() {
    const mod = { kind: "Module", functions: [], prototypes: [], groups: [] };
    const builtin = [
      {
        name: "Exception",
        parent: null,
        fields: [
          { name: "file", type: { kind: "PrimitiveType", name: "string" } },
          { name: "line", type: { kind: "PrimitiveType", name: "int" } },
          { name: "column", type: { kind: "PrimitiveType", name: "int" } },
          { name: "message", type: { kind: "PrimitiveType", name: "string" } },
          { name: "cause", type: { kind: "NamedType", name: "Exception" } },
        ],
      },
      {
        name: "RuntimeException",
        parent: "Exception",
        fields: [
          { name: "code", type: { kind: "PrimitiveType", name: "string" } },
          { name: "category", type: { kind: "PrimitiveType", name: "string" } },
        ],
      },
      {
        name: "CivilDateTime",
        parent: null,
        fields: [
          { name: "year", type: { kind: "PrimitiveType", name: "int" } },
          { name: "month", type: { kind: "PrimitiveType", name: "int" } },
          { name: "day", type: { kind: "PrimitiveType", name: "int" } },
          { name: "hour", type: { kind: "PrimitiveType", name: "int" } },
          { name: "minute", type: { kind: "PrimitiveType", name: "int" } },
          { name: "second", type: { kind: "PrimitiveType", name: "int" } },
          { name: "millisecond", type: { kind: "PrimitiveType", name: "int" } },
        ],
      },
      {
        name: "PathInfo",
        parent: null,
        fields: [
          { name: "dirname", type: { kind: "PrimitiveType", name: "string" } },
          { name: "basename", type: { kind: "PrimitiveType", name: "string" } },
          { name: "filename", type: { kind: "PrimitiveType", name: "string" } },
          { name: "extension", type: { kind: "PrimitiveType", name: "string" } },
        ],
      },
      {
        name: "PathEntry",
        parent: null,
        fields: [
          { name: "path", type: { kind: "PrimitiveType", name: "string" } },
          { name: "name", type: { kind: "PrimitiveType", name: "string" } },
          { name: "depth", type: { kind: "PrimitiveType", name: "int" } },
          { name: "isDir", type: { kind: "PrimitiveType", name: "bool" } },
          { name: "isFile", type: { kind: "PrimitiveType", name: "bool" } },
          { name: "isSymlink", type: { kind: "PrimitiveType", name: "bool" } },
        ],
      },
      {
        name: "ProcessEvent",
        parent: null,
        fields: [
          { name: "stream", type: { kind: "PrimitiveType", name: "int" } },
          { name: "data", type: { kind: "GenericType", name: "list", args: [{ kind: "PrimitiveType", name: "byte" }] } },
        ],
      },
      {
        name: "ProcessResult",
        parent: null,
        fields: [
          { name: "exitCode", type: { kind: "PrimitiveType", name: "int" } },
          { name: "events", type: { kind: "GenericType", name: "list", args: [{ kind: "NamedType", name: "ProcessEvent" }] } },
        ],
      },
      { name: "DSTAmbiguousTimeException", parent: "Exception", fields: [] },
      { name: "DSTNonExistentTimeException", parent: "Exception", fields: [] },
      { name: "InvalidTimeZoneException", parent: "Exception", fields: [] },
      { name: "InvalidDateException", parent: "Exception", fields: [] },
      { name: "InvalidISOFormatException", parent: "Exception", fields: [] },
      { name: "InvalidModeException", parent: "RuntimeException", fields: [] },
      { name: "FileOpenException", parent: "RuntimeException", fields: [] },
      { name: "FileNotFoundException", parent: "RuntimeException", fields: [] },
      { name: "PermissionDeniedException", parent: "RuntimeException", fields: [] },
      { name: "InvalidPathException", parent: "RuntimeException", fields: [] },
      { name: "FileClosedException", parent: "RuntimeException", fields: [] },
      { name: "InvalidArgumentException", parent: "RuntimeException", fields: [] },
      { name: "ProcessCreationException", parent: "RuntimeException", fields: [] },
      { name: "ProcessExecutionException", parent: "RuntimeException", fields: [] },
      { name: "ProcessPermissionException", parent: "RuntimeException", fields: [] },
      { name: "InvalidExecutableException", parent: "RuntimeException", fields: [] },
      { name: "EnvironmentAccessException", parent: "RuntimeException", fields: [] },
      { name: "InvalidEnvironmentNameException", parent: "RuntimeException", fields: [] },
      { name: "InvalidGlyphPositionException", parent: "RuntimeException", fields: [] },
      { name: "ReadFailureException", parent: "RuntimeException", fields: [] },
      { name: "WriteFailureException", parent: "RuntimeException", fields: [] },
      { name: "Utf8DecodeException", parent: "RuntimeException", fields: [] },
      { name: "StandardStreamCloseException", parent: "RuntimeException", fields: [] },
      { name: "IOException", parent: "RuntimeException", fields: [] },
      { name: "NotADirectoryException", parent: "RuntimeException", fields: [] },
      { name: "NotAFileException", parent: "RuntimeException", fields: [] },
      { name: "DirectoryNotEmptyException", parent: "RuntimeException", fields: [] },
    ];
    const builtinNames = [];
    for (const b of builtin) {
      if (this.prototypes.has(b.name)) continue;
      const methods = new Map();
      this.prototypes.set(b.name, { name: b.name, parent: b.parent, fields: b.fields, methods });
      const fieldsIr = b.fields.map((f) => ({ name: f.name, type: lowerType(f.type) }));
      mod.prototypes.push({ name: b.name, parent: b.parent, fields: fieldsIr, methods: [] });
      builtinNames.push(b.name);
    }
    for (const d of this.ast.decls) {
      if (d.kind === "PrototypeDecl") {
        const fields = (d.fields || []).map((f) => ({ name: f.name, type: f.type }));
        const fieldsIr = (d.fields || []).map((f) => ({ name: f.name, type: lowerType(f.type) }));
        const methods = new Map();
        const methodsIr = [];
        for (const m of d.methods || []) {
          methods.set(m.name, m);
          methodsIr.push({
            name: m.name,
            params: (m.params || []).map((p) => ({ name: p.name, type: lowerType(p.type), variadic: !!p.variadic })),
            returnType: lowerType(m.retType),
          });
        }
        this.prototypes.set(d.name, { name: d.name, parent: d.parent || null, fields, methods, sealed: !!d.sealed });
        mod.prototypes.push({ name: d.name, parent: d.parent || null, fields: fieldsIr, methods: methodsIr, sealed: !!d.sealed });
      }
    }
    for (const d of this.ast.decls) {
      if (d.kind === "FunctionDecl") this.functions.set(d.name, d);
      if (d.kind === "PrototypeDecl") {
        for (const m of d.methods || []) this.functions.set(`${d.name}.${m.name}`, m);
      }
    }
    for (const d of this.ast.decls) {
      if (d.kind === "FunctionDecl") mod.functions.push(this.lowerFunction(d));
      if (d.kind === "PrototypeDecl") {
        mod.functions.push(this.lowerCloneFunction(d.name));
        for (const m of d.methods || []) mod.functions.push(this.lowerMethod(d.name, m));
      }
    }
    for (const name of builtinNames) {
      mod.functions.push(this.lowerCloneFunction(name));
    }
    mod.groups = this.buildGroups();
    return mod;
  }

  lowerMethod(protoName, method) {
    const entry = { kind: "Block", label: "entry", instrs: [] };
    const params = [];
    const irFn = {
      kind: "Function",
      name: `${protoName}.${method.name}`,
      params,
      returnType: lowerType(method.retType),
      blocks: [entry],
    };
    const scope = new Scope(null);
    const selfName = this.nextVar("self");
    scope.define("self", { kind: "NamedType", name: protoName }, selfName);
    params.push({ name: selfName, type: lowerType({ kind: "NamedType", name: protoName }), variadic: false });
    for (const p of method.params) {
      const irName = this.nextVar(p.name);
      scope.define(p.name, p.type, irName);
      params.push({ name: irName, type: lowerType(p.type), variadic: !!p.variadic });
    }
    this.lowerBlock(method.body, entry, irFn, scope);
    return irFn;
  }

  lowerCloneFunction(protoName) {
    const entry = { kind: "Block", label: "entry", instrs: [] };
    const irFn = {
      kind: "Function",
      name: `${protoName}.clone`,
      params: [],
      returnType: lowerType({ kind: "NamedType", name: protoName }),
      blocks: [entry],
    };
    const dst = this.nextTemp();
    this.emit(entry, { op: "make_object", dst, proto: protoName });
    const fields = this.collectPrototypeFields(protoName);
    const core = new Set(["file", "line", "column", "message", "cause", "code", "category"]);
    const isException = this.isExceptionProto(protoName);
    for (const f of fields) {
      if (isException && core.has(f.name)) continue;
      const v = this.emitDefaultValue(entry, f.type);
      this.emit(entry, { op: "member_set", target: dst, name: f.name, src: v.value });
    }
    this.emit(entry, { op: "ret", value: dst });
    return irFn;
  }

  isExceptionProto(protoName) {
    let cur = this.prototypes.get(protoName);
    while (cur) {
      if (cur.name === "Exception") return true;
      cur = cur.parent ? this.prototypes.get(cur.parent) : null;
    }
    return false;
  }

  collectPrototypeFields(protoName) {
    const out = [];
    const chain = [];
    let cur = this.prototypes.get(protoName);
    while (cur) {
      chain.push(cur);
      cur = cur.parent ? this.prototypes.get(cur.parent) : null;
    }
    for (let i = chain.length - 1; i >= 0; i -= 1) {
      for (const f of chain[i].fields) out.push(f);
    }
    return out;
  }

  emitDefaultValue(block, typeNode) {
    if (!typeNode || typeNode.kind === "PrimitiveType") {
      const name = typeNode ? typeNode.name : "void";
      const dst = this.nextTemp();
      if (name === "int") this.emit(block, { op: "const", dst, literalType: "int", value: "0" });
      else if (name === "byte") this.emit(block, { op: "const", dst, literalType: "byte", value: "0" });
      else if (name === "float") this.emit(block, { op: "const", dst, literalType: "float", value: "0" });
      else if (name === "bool") this.emit(block, { op: "const", dst, literalType: "bool", value: "false" });
      else if (name === "glyph") this.emit(block, { op: "const", dst, literalType: "glyph", value: "0" });
      else if (name === "string") this.emit(block, { op: "const", dst, literalType: "string", value: "" });
      else this.emit(block, { op: "const", dst, literalType: "int", value: "0" });
      return { value: dst, type: typeNode || { kind: "PrimitiveType", name: "void" } };
    }
    if (typeNode.kind === "NamedType" && this.prototypes.has(typeNode.name)) {
      const dst = this.nextTemp();
      this.emit(block, { op: "call_static", dst, callee: `${typeNode.name}.clone`, args: [], variadic: false });
      return { value: dst, type: typeNode };
    }
    if (typeNode.kind === "GenericType") {
      if (typeNode.name === "list") {
        const dst = this.nextTemp();
        this.emit(block, { op: "make_list", dst, items: [], type: lowerType(typeNode) });
        return { value: dst, type: typeNode };
      }
      if (typeNode.name === "map") {
        const dst = this.nextTemp();
        this.emit(block, { op: "make_map", dst, pairs: [], type: lowerType(typeNode) });
        return { value: dst, type: typeNode };
      }
    }
    const fallback = this.nextTemp();
    this.emit(block, { op: "const", dst: fallback, literalType: "int", value: "0" });
    return { value: fallback, type: typeNode };
  }

  resolvePrototypeField(protoName, fieldName) {
    let p = this.prototypes.get(protoName);
    while (p) {
      const hit = p.fields.find((f) => f.name === fieldName);
      if (hit) return hit.type;
      p = p.parent ? this.prototypes.get(p.parent) : null;
    }
    return null;
  }

  resolvePrototypeMethod(protoName, methodName) {
    let p = this.prototypes.get(protoName);
    while (p) {
      if (p.methods.has(methodName)) return p.methods.get(methodName);
      p = p.parent ? this.prototypes.get(p.parent) : null;
    }
    return null;
  }

  lowerFunction(fn) {
    const entry = { kind: "Block", label: "entry", instrs: [] };
    const irFn = {
      kind: "Function",
      name: fn.name,
      params: [],
      returnType: lowerType(fn.retType),
      blocks: [entry],
    };
    const scope = new Scope(null);
    for (const p of fn.params) {
      const irName = this.nextVar(p.name);
      scope.define(p.name, p.type, irName);
      irFn.params.push({ name: irName, type: lowerType(p.type), variadic: !!p.variadic });
    }
    this.lowerBlock(fn.body, entry, irFn, scope);
    return irFn;
  }

  lowerBlock(blockAst, block, irFn, scope) {
    const local = new Scope(scope);
    let cur = block;
    for (const s of blockAst.stmts) cur = this.lowerStmt(s, cur, irFn, local);
    return cur;
  }

  lowerStmt(stmt, block, irFn, scope) {
    switch (stmt.kind) {
      case "VarDecl": {
        const declared = stmt.declaredType || (stmt.init ? this.inferExprType(stmt.init, scope) : { kind: "PrimitiveType", name: "void" });
        const irName = this.nextVar(stmt.name);
        scope.define(stmt.name, declared, irName);
        this.emit(block, { op: "var_decl", name: irName, type: lowerType(declared) });
        if (stmt.init) {
          const rhs = this.lowerExpr(stmt.init, block, irFn, scope);
          this.emit(rhs.block, { op: "store_var", name: irName, src: rhs.value, type: lowerType(rhs.type) });
          return rhs.block;
        }
        if (stmt.declaredType) {
          const rhs = this.emitDefaultValue(block, stmt.declaredType);
          this.emit(block, { op: "store_var", name: irName, src: rhs.value, type: lowerType(rhs.type) });
        }
        return block;
      }
      case "AssignStmt": {
        if (stmt.op && stmt.op !== "=") {
          const binOp = stmt.op === "+=" ? "+" : stmt.op === "-=" ? "-" : stmt.op === "*=" ? "*" : stmt.op === "/=" ? "/" : null;
          if (!binOp) return block;
          const lhsType = this.inferExprType(stmt.target, scope);
          const rhsType = this.inferExprType(stmt.expr, scope);
          const rhs = this.lowerExpr(stmt.expr, block, irFn, scope);
          const curBlock = rhs.block;

          if (stmt.target.kind === "Identifier") {
            const entry = scope.get(stmt.target.name);
            const irName = entry ? entry.irName : stmt.target.name;
            const cur = this.nextTemp();
            this.emit(curBlock, { op: "load_var", dst: cur, name: irName, type: lowerType(lhsType) });
            if (["+", "-", "*"].includes(binOp) && isIntLike(lhsType) && isIntLike(rhsType)) {
              this.emit(curBlock, { op: "check_int_overflow", operator: binOp, left: cur, right: rhs.value });
            }
            if (binOp === "/" && isIntLike(lhsType) && isIntLike(rhsType)) {
              this.emit(curBlock, { op: "check_div_zero", divisor: rhs.value });
            }
            const next = this.nextTemp();
            this.emit(curBlock, { op: "bin_op", dst: next, operator: binOp, left: cur, right: rhs.value });
            this.emit(curBlock, { op: "store_var", name: irName, src: next, type: lowerType(lhsType) });
            return curBlock;
          }

          if (stmt.target.kind === "MemberExpr") {
            const base = this.lowerExpr(stmt.target.target, curBlock, irFn, scope);
            const cur = this.nextTemp();
            this.emit(base.block, { op: "member_get", dst: cur, target: base.value, name: stmt.target.name });
            if (["+", "-", "*"].includes(binOp) && isIntLike(lhsType) && isIntLike(rhsType)) {
              this.emit(base.block, { op: "check_int_overflow", operator: binOp, left: cur, right: rhs.value });
            }
            if (binOp === "/" && isIntLike(lhsType) && isIntLike(rhsType)) {
              this.emit(base.block, { op: "check_div_zero", divisor: rhs.value });
            }
            const next = this.nextTemp();
            this.emit(base.block, { op: "bin_op", dst: next, operator: binOp, left: cur, right: rhs.value });
            this.emit(base.block, { op: "member_set", target: base.value, name: stmt.target.name, src: next });
            return base.block;
          }

          if (stmt.target.kind === "IndexExpr") {
            const t = this.lowerExpr(stmt.target.target, curBlock, irFn, scope);
            const i = this.lowerExpr(stmt.target.index, t.block, irFn, scope);
            this.emitIndexChecks(i.block, t, i, { forWrite: false });
            const cur = this.nextTemp();
            this.emit(i.block, { op: "index_get", dst: cur, target: t.value, index: i.value });
            if (["+", "-", "*"].includes(binOp) && isIntLike(lhsType) && isIntLike(rhsType)) {
              this.emit(i.block, { op: "check_int_overflow", operator: binOp, left: cur, right: rhs.value });
            }
            if (binOp === "/" && isIntLike(lhsType) && isIntLike(rhsType)) {
              this.emit(i.block, { op: "check_div_zero", divisor: rhs.value });
            }
            const next = this.nextTemp();
            this.emit(i.block, { op: "bin_op", dst: next, operator: binOp, left: cur, right: rhs.value });
            this.emit(i.block, { op: "index_set", target: t.value, index: i.value, src: next });
            return i.block;
          }

          return curBlock;
        }
        let rhs = null;
        let cur = block;
        if (
          (stmt.expr.kind === "ListLiteral" && stmt.expr.items.length === 0) ||
          (stmt.expr.kind === "MapLiteral" && stmt.expr.pairs.length === 0)
        ) {
          const lhsType = this.inferAssignableType(stmt.target, scope);
          if (lhsType && lhsType.kind === "GenericType" && (lhsType.name === "list" || lhsType.name === "map")) {
            rhs = this.emitDefaultValue(block, lhsType);
          }
        }
        if (!rhs) {
          rhs = this.lowerExpr(stmt.expr, block, irFn, scope);
          cur = rhs.block;
        }
        if (stmt.target.kind === "Identifier") {
          const entry = scope.get(stmt.target.name);
          const irName = entry ? entry.irName : stmt.target.name;
          this.emit(cur, { op: "store_var", name: irName, src: rhs.value, type: lowerType(rhs.type) });
        } else if (stmt.target.kind === "IndexExpr") {
          const t = this.lowerExpr(stmt.target.target, cur, irFn, scope);
          const i = this.lowerExpr(stmt.target.index, t.block, irFn, scope);
          this.emitIndexChecks(i.block, t, i, { forWrite: true });
          this.emit(i.block, { op: "index_set", target: t.value, index: i.value, src: rhs.value });
          cur = i.block;
        } else if (stmt.target.kind === "MemberExpr") {
          const t = this.lowerExpr(stmt.target.target, cur, irFn, scope);
          this.emit(t.block, { op: "member_set", target: t.value, name: stmt.target.name, src: rhs.value });
          cur = t.block;
        }
        return cur;
      }
      case "ExprStmt":
        return this.lowerExpr(stmt.expr, block, irFn, scope).block;
      case "ReturnStmt":
        if (stmt.expr) {
          const v = this.lowerExpr(stmt.expr, block, irFn, scope);
          this.emit(v.block, { op: "ret", value: v.value, type: lowerType(v.type) });
          return v.block;
        } else {
          this.emit(block, { op: "ret_void" });
          return block;
        }
      case "ThrowStmt": {
        const v = this.lowerExpr(stmt.expr, block, irFn, scope);
        this.emit(v.block, { op: "throw", value: v.value });
        return v.block;
      }
      case "ForStmt":
        return this.lowerFor(stmt, block, irFn, scope);
      case "IfStmt":
        return this.lowerIf(stmt, block, irFn, scope);
      case "WhileStmt":
        return this.lowerWhile(stmt, block, irFn, scope);
      case "DoWhileStmt":
        return this.lowerDoWhile(stmt, block, irFn, scope);
      case "SwitchStmt":
        return this.lowerSwitch(stmt, block, irFn, scope);
      case "Block":
        return this.lowerBlock(stmt, block, irFn, scope);
      case "TryStmt": {
        const catches = stmt.catches || [];
        const hasCatch = catches.length > 0;
        const hasFinally = !!stmt.finallyBlock;
        const tryLabel = this.nextBlock("try_body_");
        const dispatchLabel = hasCatch ? this.nextBlock("try_dispatch_") : null;
        const catchLabels = catches.map(() => this.nextBlock("try_catch_"));
        const dispatchChecks = hasCatch ? catches.map((_, i) => (i === 0 ? dispatchLabel : this.nextBlock("try_dispatch_"))) : [];
        const rethrowLabel = hasCatch ? this.nextBlock("try_rethrow_") : null;
        const finallyLabel = hasFinally ? this.nextBlock("try_finally_") : null;
        const finallyRethrowLabel = hasFinally && !hasCatch ? this.nextBlock("try_finally_rethrow_") : null;
        const doneLabel = this.nextBlock("try_done_");
        const bTry = { kind: "Block", label: tryLabel, instrs: [] };
        const bFinally = finallyLabel ? { kind: "Block", label: finallyLabel, instrs: [] } : null;
        const bFinallyRethrow = finallyRethrowLabel ? { kind: "Block", label: finallyRethrowLabel, instrs: [] } : null;
        const bDone = { kind: "Block", label: doneLabel, instrs: [] };
        const handlerTarget = dispatchLabel || finallyRethrowLabel || finallyLabel || doneLabel;

        this.emit(block, { op: "push_handler", target: handlerTarget });
        this.emit(block, { op: "jump", target: tryLabel });
        irFn.blocks.push(bTry);
        const tryEnd = this.lowerBlock(stmt.tryBlock, bTry, irFn, new Scope(scope));
        this.emit(tryEnd, { op: "pop_handler" });
        this.emit(tryEnd, { op: "jump", target: finallyLabel || doneLabel });

        let exTemp = null;
        if (hasCatch) {
          exTemp = this.nextTemp();
          for (const label of dispatchChecks) {
            irFn.blocks.push({ kind: "Block", label, instrs: [] });
          }
          const bRethrow = { kind: "Block", label: rethrowLabel, instrs: [] };
          irFn.blocks.push(bRethrow);
          for (const label of catchLabels) {
            irFn.blocks.push({ kind: "Block", label, instrs: [] });
          }
          if (bFinally) irFn.blocks.push(bFinally);
          if (bFinallyRethrow) irFn.blocks.push(bFinallyRethrow);
          irFn.blocks.push(bDone);

          // Dispatch checks
          for (let i = 0; i < catches.length; i += 1) {
            const bCheck = irFn.blocks.find((b) => b.label === dispatchChecks[i]);
            if (i === 0) this.emit(bCheck, { op: "get_exception", dst: exTemp });
            const cond = this.nextTemp();
            this.emit(bCheck, { op: "exception_is", dst: cond, value: exTemp, type: typeToString(catches[i].type) });
            const elseLabel = i + 1 < catches.length ? dispatchChecks[i + 1] : rethrowLabel;
            this.emit(bCheck, { op: "branch_if", cond, then: catchLabels[i], else: elseLabel });
          }
          this.emit(bRethrow, { op: "rethrow" });

          // Catch blocks
          for (let i = 0; i < catches.length; i += 1) {
            const c = catches[i];
            const bCatch = irFn.blocks.find((b) => b.label === catchLabels[i]);
            const cs = new Scope(scope);
            const irName = this.nextVar(c.name);
            cs.define(c.name, c.type, irName);
            this.emit(bCatch, { op: "var_decl", name: irName, type: lowerType(c.type) });
            this.emit(bCatch, { op: "store_var", name: irName, src: exTemp, type: lowerType(c.type) });
            const catchEnd = this.lowerBlock(c.block, bCatch, irFn, cs);
            this.emit(catchEnd, { op: "jump", target: finallyLabel || doneLabel });
          }
        } else {
          if (bFinally) irFn.blocks.push(bFinally);
          if (bFinallyRethrow) irFn.blocks.push(bFinallyRethrow);
          irFn.blocks.push(bDone);
        }

        if (bFinally) {
          const finallyEnd = this.lowerBlock(stmt.finallyBlock, bFinally, irFn, new Scope(scope));
          this.emit(finallyEnd, { op: "jump", target: doneLabel });
        }
        if (bFinallyRethrow) {
          const finallyEnd = this.lowerBlock(stmt.finallyBlock, bFinallyRethrow, irFn, new Scope(scope));
          this.emit(finallyEnd, { op: "rethrow" });
        }
        this.emit(bDone, { op: "nop" });
        return bDone;
      }
      case "BreakStmt":
        if (this.breakStack.length > 0) {
          this.emit(block, { op: "jump", target: this.breakStack[this.breakStack.length - 1] });
        } else {
          this.emit(block, { op: "unhandled_stmt", kind: stmt.kind });
        }
        return block;
      case "ContinueStmt":
        if (this.continueStack.length > 0) {
          this.emit(block, { op: "jump", target: this.continueStack[this.continueStack.length - 1] });
        } else {
          this.emit(block, { op: "unhandled_stmt", kind: stmt.kind });
        }
        return block;
      default:
        this.emit(block, { op: "unhandled_stmt", kind: stmt.kind });
        return block;
    }
  }

  lowerFor(stmt, block, irFn, scope) {
    if (stmt.forKind === "classic") {
      const init = this.nextBlock("for_init_");
      const cond = this.nextBlock("for_cond_");
      const body = this.nextBlock("for_body_");
      const step = this.nextBlock("for_step_");
      const done = this.nextBlock("for_done_");
      const forScope = new Scope(scope);
      const bInit = { kind: "Block", label: init, instrs: [] };
      const bCond = { kind: "Block", label: cond, instrs: [] };
      const bBody = { kind: "Block", label: body, instrs: [] };
      const bStep = { kind: "Block", label: step, instrs: [] };
      const bDone = { kind: "Block", label: done, instrs: [] };
      irFn.blocks.push(bInit, bCond, bBody, bStep);
      this.emit(block, { op: "jump", target: init });
      const bInitEnd = stmt.init ? this.lowerStmtLikeForPart(stmt.init, bInit, irFn, forScope) : bInit;
      this.emit(bInitEnd, { op: "jump", target: cond });
      if (stmt.cond) {
        const c = this.lowerExpr(stmt.cond, bCond, irFn, forScope);
        this.emit(c.block, { op: "branch_if", cond: c.value, then: body, else: done });
      } else {
        this.emit(bCond, { op: "jump", target: body });
      }
      this.pushLoopTargets(done, step);
      const bodyEnd = this.lowerStmt(stmt.body, bBody, irFn, new Scope(forScope));
      this.emit(bodyEnd, { op: "jump", target: step });
      this.popLoopTargets();
      const bStepEnd = stmt.step ? this.lowerStmtLikeForPart(stmt.step, bStep, irFn, forScope) : bStep;
      this.emit(bStepEnd, { op: "jump", target: cond });
      irFn.blocks.push(bDone);
      this.emit(bDone, { op: "nop" });
      return bDone;
    }

    const init = this.nextBlock(`for_${stmt.forKind}_init_`);
    const cond = this.nextBlock(`for_${stmt.forKind}_cond_`);
    const body = this.nextBlock(`for_${stmt.forKind}_body_`);
    const done = this.nextBlock(`for_${stmt.forKind}_done_`);
    const bInit = { kind: "Block", label: init, instrs: [] };
    const bCond = { kind: "Block", label: cond, instrs: [] };
    const bBody = { kind: "Block", label: body, instrs: [] };
    const bDone = { kind: "Block", label: done, instrs: [] };
    irFn.blocks.push(bInit, bCond, bBody);

    const seq = this.lowerExpr(stmt.iterExpr, bInit, irFn, scope);
    const cursor = this.nextTemp();
    this.emit(block, { op: "jump", target: init });
    this.emit(seq.block, { op: "iter_begin", dst: cursor, source: seq.value, mode: stmt.forKind });
    this.emit(seq.block, { op: "jump", target: cond });
    this.emit(bCond, { op: "branch_iter_has_next", iter: cursor, then: body, else: done });

    const sBody = new Scope(scope);
    const elem = this.nextTemp();
    this.emit(bBody, { op: "iter_next", dst: elem, iter: cursor, source: seq.value, mode: stmt.forKind });
    if (stmt.iterVar) {
      const vt = stmt.iterVar.declaredType || this.elementTypeForIter(seq.type, stmt.forKind);
      const irName = this.nextVar(stmt.iterVar.name);
      sBody.define(stmt.iterVar.name, vt, irName);
      this.emit(bBody, { op: "var_decl", name: irName, type: lowerType(vt) });
      this.emit(bBody, { op: "store_var", name: irName, src: elem, type: lowerType(vt) });
    }
    this.pushLoopTargets(done, cond);
    const bodyEnd = this.lowerStmt(stmt.body, bBody, irFn, sBody);
    this.emit(bodyEnd, { op: "jump", target: cond });
    this.popLoopTargets();
    irFn.blocks.push(bDone);
    this.emit(bDone, { op: "nop" });
    return bDone;
  }

  lowerWhile(stmt, block, irFn, scope) {
    const cond = this.nextBlock("while_cond_");
    const body = this.nextBlock("while_body_");
    const done = this.nextBlock("while_done_");
    const bCond = { kind: "Block", label: cond, instrs: [] };
    const bBody = { kind: "Block", label: body, instrs: [] };
    const bDone = { kind: "Block", label: done, instrs: [] };
    irFn.blocks.push(bCond, bBody, bDone);
    this.emit(block, { op: "jump", target: cond });
    const c = this.lowerExpr(stmt.cond, bCond, irFn, scope);
    this.emit(c.block, { op: "branch_if", cond: c.value, then: body, else: done });
    this.pushLoopTargets(done, cond);
    const bodyEnd = this.lowerStmt(stmt.body, bBody, irFn, new Scope(scope));
    this.emit(bodyEnd, { op: "jump", target: cond });
    this.popLoopTargets();
    this.emit(bDone, { op: "nop" });
    return bDone;
  }

  lowerDoWhile(stmt, block, irFn, scope) {
    const body = this.nextBlock("do_body_");
    const cond = this.nextBlock("do_cond_");
    const done = this.nextBlock("do_done_");
    const bBody = { kind: "Block", label: body, instrs: [] };
    const bCond = { kind: "Block", label: cond, instrs: [] };
    const bDone = { kind: "Block", label: done, instrs: [] };
    irFn.blocks.push(bBody, bCond, bDone);
    this.emit(block, { op: "jump", target: body });
    this.pushLoopTargets(done, cond);
    const bodyEnd = this.lowerStmt(stmt.body, bBody, irFn, new Scope(scope));
    this.emit(bodyEnd, { op: "jump", target: cond });
    this.popLoopTargets();
    const c = this.lowerExpr(stmt.cond, bCond, irFn, scope);
    this.emit(c.block, { op: "branch_if", cond: c.value, then: body, else: done });
    this.emit(bDone, { op: "nop" });
    return bDone;
  }

  lowerIf(stmt, block, irFn, scope) {
    const cond = this.lowerExpr(stmt.cond, block, irFn, scope);
    const thenLabel = this.nextBlock("if_then_");
    const doneLabel = this.nextBlock("if_done_");
    const elseLabel = stmt.elseBranch ? this.nextBlock("if_else_") : doneLabel;
    const bThen = { kind: "Block", label: thenLabel, instrs: [] };
    const bElse = stmt.elseBranch ? { kind: "Block", label: elseLabel, instrs: [] } : null;
    const bDone = { kind: "Block", label: doneLabel, instrs: [] };
    irFn.blocks.push(bThen);
    if (bElse) irFn.blocks.push(bElse);

    this.emit(cond.block, { op: "branch_if", cond: cond.value, then: thenLabel, else: elseLabel });
    const thenEnd = this.lowerStmt(stmt.thenBranch, bThen, irFn, new Scope(scope));
    if (!this.blockHasTerminator(thenEnd)) this.emit(thenEnd, { op: "jump", target: doneLabel });
    if (bElse) {
      const elseEnd = this.lowerStmt(stmt.elseBranch, bElse, irFn, new Scope(scope));
      if (!this.blockHasTerminator(elseEnd)) this.emit(elseEnd, { op: "jump", target: doneLabel });
    }
    irFn.blocks.push(bDone);
    this.emit(bDone, { op: "nop" });
    return bDone;
  }

  blockHasTerminator(block) {
    if (!block || !block.instrs || block.instrs.length === 0) return false;
    const op = block.instrs[block.instrs.length - 1]?.op;
    return (
      op === "jump" ||
      op === "branch_if" ||
      op === "branch_iter_has_next" ||
      op === "ret" ||
      op === "ret_void" ||
      op === "throw" ||
      op === "rethrow"
    );
  }

  lowerStmtLikeForPart(node, block, irFn, scope) {
    if (node.kind === "VarDecl" || node.kind === "AssignStmt") return this.lowerStmt(node, block, irFn, scope);
    return this.lowerExpr(node, block, irFn, scope).block;
  }

  lowerSwitch(stmt, block, irFn, scope) {
    const value = this.lowerExpr(stmt.expr, block, irFn, scope);
    const done = this.nextBlock("switch_done_");
    const doneBlock = { kind: "Block", label: done, instrs: [] };
    this.pushBreakTarget(done);

    let nextLabel = null;
    for (let idx = 0; idx < stmt.cases.length; idx += 1) {
      const c = stmt.cases[idx];
      const cmpLabel = nextLabel || this.nextBlock("switch_casecmp_");
      const bodyLabel = this.nextBlock("switch_casebody_");
      nextLabel = this.nextBlock("switch_casecmp_");
      const bCmp = { kind: "Block", label: cmpLabel, instrs: [] };
      const bBody = { kind: "Block", label: bodyLabel, instrs: [] };
      irFn.blocks.push(bCmp, bBody);
      if (idx === 0) this.emit(value.block, { op: "jump", target: cmpLabel });
      const cv = this.lowerExpr(c.value, bCmp, irFn, scope);
      const eq = this.nextTemp();
      this.emit(cv.block, { op: "bin_op", dst: eq, operator: "==", left: value.value, right: cv.value });
      this.emit(cv.block, { op: "branch_if", cond: eq, then: bodyLabel, else: nextLabel });
      const sBody = new Scope(scope);
      let cur = bBody;
      for (const st of c.stmts) cur = this.lowerStmt(st, cur, irFn, sBody);
      this.emit(cur, { op: "jump", target: done });
    }

    const defaultLabel = stmt.defaultCase ? this.nextBlock("switch_default_") : done;
    if (nextLabel) {
      const bNext = { kind: "Block", label: nextLabel, instrs: [] };
      irFn.blocks.push(bNext);
      this.emit(bNext, { op: "jump", target: defaultLabel });
    }
    if (stmt.defaultCase) {
      const bDefault = { kind: "Block", label: defaultLabel, instrs: [] };
      irFn.blocks.push(bDefault);
      let cur = bDefault;
      for (const st of stmt.defaultCase.stmts) cur = this.lowerStmt(st, cur, irFn, new Scope(scope));
      this.emit(cur, { op: "jump", target: done });
    }
    irFn.blocks.push(doneBlock);
    this.emit(doneBlock, { op: "nop" });
    this.popBreakTarget();
    return doneBlock;
  }

  lowerExpr(expr, block, irFn, scope) {
    switch (expr.kind) {
      case "Literal": {
        const dst = this.nextTemp();
        const t = { kind: "PrimitiveType", name: expr.literalType };
        this.emit(block, { op: "const", dst, literalType: expr.literalType, value: expr.value });
        return { value: dst, type: t, block };
      }
      case "CastExpr": {
        const inner = this.lowerExpr(expr.expr, block, irFn, scope);
        const targetType = expr.targetType;
        const targetName = typeToString(targetType);
        if (targetName === typeToString(inner.type)) return { value: inner.value, type: targetType, block: inner.block };
        const dst = this.nextTemp();
        if (targetName === "byte") {
          if (typeToString(inner.type) === "float") {
            const tmp = this.nextTemp();
            this.emit(inner.block, { op: "call_method_static", dst: tmp, receiver: inner.value, method: "toInt", args: [] });
            this.emit(inner.block, { op: "call_method_static", dst, receiver: tmp, method: "toByte", args: [] });
          } else {
            this.emit(inner.block, { op: "call_method_static", dst, receiver: inner.value, method: "toByte", args: [] });
          }
        } else if (targetName === "int") {
          this.emit(inner.block, { op: "call_method_static", dst, receiver: inner.value, method: "toInt", args: [] });
        } else if (targetName === "float") {
          this.emit(inner.block, { op: "call_method_static", dst, receiver: inner.value, method: "toFloat", args: [] });
        } else {
          this.emit(inner.block, { op: "copy", dst, src: inner.value });
        }
        return { value: dst, type: targetType, block: inner.block };
      }
      case "Identifier": {
        const dst = this.nextTemp();
        const entry = scope.get(expr.name);
        if (!entry && this.groupConsts.has(expr.name)) {
          this.emit(block, { op: "const", dst, literalType: "group", value: expr.name });
          return { value: dst, type: { kind: "NamedType", name: "group" }, block };
        }
        const t = entry ? entry.type : { kind: "PrimitiveType", name: "unknown" };
        const irName = entry ? entry.irName : expr.name;
        this.emit(block, { op: "load_var", dst, name: irName, type: lowerType(t) });
        return { value: dst, type: t, block };
      }
      case "UnaryExpr": {
        if (expr.op === "-" && expr.expr && expr.expr.kind === "Literal" && expr.expr.literalType === "int") {
          const v = intLiteralToBigInt(expr.expr.value);
          if (v === 9223372036854775808n) {
            const dst = this.nextTemp();
            this.emit(block, { op: "const", dst, literalType: "int", value: "-9223372036854775808" });
            return { value: dst, type: { kind: "PrimitiveType", name: "int" }, block };
          }
        }
        if (expr.op === "++" || expr.op === "--") {
          return this.lowerIncDec(expr.expr, block, irFn, scope, true, expr.op);
        }
        const v = this.lowerExpr(expr.expr, block, irFn, scope);
        const dst = this.nextTemp();
        if (expr.op === "-" && isIntLike(v.type)) {
          this.emit(v.block, { op: "check_int_overflow_unary_minus", value: v.value });
        }
        this.emit(v.block, { op: "unary_op", dst, operator: expr.op, src: v.value });
        return { value: dst, type: v.type, block: v.block };
      }
      case "BinaryExpr": {
        const l = this.lowerExpr(expr.left, block, irFn, scope);
        if (expr.op === "&&" || expr.op === "||") {
          const rightLabel = this.nextBlock("logic_right_");
          const shortLabel = this.nextBlock("logic_short_");
          const doneLabel = this.nextBlock("logic_done_");
          const bRight = { kind: "Block", label: rightLabel, instrs: [] };
          const bShort = { kind: "Block", label: shortLabel, instrs: [] };
          const bDone = { kind: "Block", label: doneLabel, instrs: [] };
          irFn.blocks.push(bRight, bShort, bDone);

          this.emit(l.block, {
            op: "branch_if",
            cond: l.value,
            then: expr.op === "&&" ? rightLabel : shortLabel,
            else: expr.op === "&&" ? shortLabel : rightLabel,
          });

          const dst = this.nextTemp();
          this.emit(bShort, {
            op: "const",
            dst,
            literalType: "bool",
            value: expr.op === "&&" ? "false" : "true",
          });
          this.emit(bShort, { op: "jump", target: doneLabel });

          const r = this.lowerExpr(expr.right, bRight, irFn, scope);
          this.emit(r.block, { op: "copy", dst, src: r.value });
          this.emit(r.block, { op: "jump", target: doneLabel });

          const contLabel = this.nextBlock("logic_cont_");
          const bCont = { kind: "Block", label: contLabel, instrs: [] };
          irFn.blocks.push(bCont);
          this.emit(bDone, { op: "nop" });
          this.emit(bDone, { op: "jump", target: contLabel });
          return { value: dst, type: { kind: "PrimitiveType", name: "bool" }, block: bCont };
        }
        const r = this.lowerExpr(expr.right, l.block, irFn, scope);
        if (["+", "-", "*"].includes(expr.op) && isIntLike(l.type) && isIntLike(r.type)) {
          this.emit(r.block, { op: "check_int_overflow", operator: expr.op, left: l.value, right: r.value });
        }
        if (["/", "%"].includes(expr.op) && isIntLike(l.type) && isIntLike(r.type)) {
          this.emit(r.block, { op: "check_div_zero", divisor: r.value });
        }
        if (["<<", ">>"].includes(expr.op) && isIntLike(l.type) && isIntLike(r.type)) {
          this.emit(r.block, { op: "check_shift_range", shift: r.value, width: typeToString(l.type) === "byte" ? 8 : 64 });
        }
        const dst = this.nextTemp();
        this.emit(r.block, { op: "bin_op", dst, operator: expr.op, left: l.value, right: r.value });
        const outType = ["==", "!=", "<", "<=", ">", ">="].includes(expr.op)
          ? { kind: "PrimitiveType", name: "bool" }
          : l.type;
        return { value: dst, type: outType, block: r.block };
      }
      case "ConditionalExpr": {
        const c = this.lowerExpr(expr.cond, block, irFn, scope);
        const thenV = this.lowerExpr(expr.thenExpr, c.block, irFn, scope);
        const elseV = this.lowerExpr(expr.elseExpr, thenV.block, irFn, scope);
        const dst = this.nextTemp();
        this.emit(elseV.block, {
          op: "select",
          dst,
          cond: c.value,
          thenValue: thenV.value,
          elseValue: elseV.value,
        });
        return { value: dst, type: thenV.type, block: elseV.block };
      }
      case "CallExpr":
        return this.lowerCallExpr(expr, block, irFn, scope);
      case "IndexExpr": {
        const t = this.lowerExpr(expr.target, block, irFn, scope);
        const i = this.lowerExpr(expr.index, t.block, irFn, scope);
        this.emitIndexChecks(i.block, t, i, { forWrite: false });
        const dst = this.nextTemp();
        this.emit(i.block, { op: "index_get", dst, target: t.value, index: i.value });
        return { value: dst, type: this.elementTypeForIndex(t.type), block: i.block };
      }
      case "MemberExpr": {
        if (expr.target.kind === "Identifier" && this.groupConsts.has(expr.target.name)) {
          const entry = this.groupConsts.get(expr.target.name);
          const members = entry ? entry.members : null;
          if (members && members.has(expr.name)) {
            const c = members.get(expr.name);
            const dst = this.nextTemp();
            this.emit(block, { op: "const", dst, literalType: c.literalType, value: c.value });
            return { value: dst, type: { kind: "PrimitiveType", name: c.literalType }, block };
          }
        }
        if (expr.target.kind === "Identifier" && this.importNamespaces.has(expr.target.name)) {
          const mod = this.importNamespaces.get(expr.target.name);
          const consts = this.moduleConsts.get(mod);
          if (consts && consts.has(expr.name)) {
            const c = consts.get(expr.name);
            const dst = this.nextTemp();
            this.emit(block, { op: "const", dst, literalType: c.type, value: c.value });
            if (c.type === "float") return { value: dst, type: { kind: "PrimitiveType", name: "float" }, block };
            if (c.type === "int") return { value: dst, type: { kind: "PrimitiveType", name: "int" }, block };
            if (c.type === "string") return { value: dst, type: { kind: "PrimitiveType", name: "string" }, block };
            if (c.type === "TextFile" || c.type === "BinaryFile") return { value: dst, type: { kind: "NamedType", name: c.type }, block };
            return { value: dst, type: { kind: "PrimitiveType", name: "unknown" }, block };
          }
        }
        const base = this.lowerExpr(expr.target, block, irFn, scope);
        const dst = this.nextTemp();
        this.emit(base.block, { op: "member_get", dst, target: base.value, name: expr.name });
        if (base.type && base.type.kind === "NamedType" && this.prototypes.has(base.type.name)) {
          const ft = this.resolvePrototypeField(base.type.name, expr.name);
          if (ft) return { value: dst, type: ft, block: base.block };
        }
        return { value: dst, type: { kind: "PrimitiveType", name: "unknown" }, block: base.block };
      }
      case "PostfixExpr": {
        if (expr.op === "++" || expr.op === "--") {
          return this.lowerIncDec(expr.expr, block, irFn, scope, false, expr.op);
        }
        const v = this.lowerExpr(expr.expr, block, irFn, scope);
        const dst = this.nextTemp();
        this.emit(v.block, { op: "postfix_op", dst, operator: expr.op, src: v.value });
        return { value: dst, type: v.type, block: v.block };
      }
      case "ListLiteral": {
        const dst = this.nextTemp();
        let cur = block;
        const vals = [];
        for (const it of expr.items) {
          const v = this.lowerExpr(it, cur, irFn, scope);
          vals.push(v);
          cur = v.block;
        }
        const elemType = vals.length > 0 ? vals[0].type : { kind: "PrimitiveType", name: "void" };
        this.emit(cur, { op: "make_list", dst, items: vals.map((v) => v.value), type: lowerType({ kind: "GenericType", name: "list", args: [elemType] }) });
        return { value: dst, type: { kind: "GenericType", name: "list", args: [elemType] }, block: cur };
      }
      case "MapLiteral": {
        const dst = this.nextTemp();
        let cur = block;
        const pairs = [];
        for (const p of expr.pairs) {
          const key = this.lowerExpr(p.key, cur, irFn, scope);
          const value = this.lowerExpr(p.value, key.block, irFn, scope);
          pairs.push({ key, value });
          cur = value.block;
        }
        let keyType = pairs.length > 0 ? pairs[0].key.type : { kind: "PrimitiveType", name: "void" };
        let valType = pairs.length > 0 ? pairs[0].value.type : { kind: "PrimitiveType", name: "void" };
        if (pairs.length > 1) {
          let mixedVal = false;
          for (let i = 1; i < pairs.length; i += 1) {
            if (typeToString(pairs[i].value.type) !== typeToString(valType)) {
              mixedVal = true;
              break;
            }
          }
          if (mixedVal && typeToString(keyType) === "string") {
            valType = { kind: "NamedType", name: "JSONValue" };
          }
        }
        this.emit(cur, {
          op: "make_map",
          dst,
          pairs: pairs.map((p) => ({ key: p.key.value, value: p.value.value })),
          type: lowerType({ kind: "GenericType", name: "map", args: [keyType, valType] }),
        });
        return { value: dst, type: { kind: "GenericType", name: "map", args: [keyType, valType] }, block: cur };
      }
      default: {
        const dst = this.nextTemp();
        this.emit(block, { op: "unknown_expr", dst, kind: expr.kind });
        return { value: dst, type: { kind: "PrimitiveType", name: "unknown" }, block };
      }
    }
  }

  lowerCallExpr(expr, block, irFn, scope) {
    const lowerArgs = (args, startBlock) => {
      let cur = startBlock;
      const out = [];
      for (const a of args) {
        const v = this.lowerExpr(a, cur, irFn, scope);
        out.push(v);
        cur = v.block;
      }
      return { args: out, block: cur };
    };

    if (expr.callee.kind === "Identifier") {
      const local = expr.callee.name;
      const callee = this.importedSymbols.get(local) || local;
      const lowered = lowerArgs(expr.args, block);
      const args = lowered.args;
      const sig = this.functions.get(local);
      const isVariadic = sig ? sig.params.some((p) => p.variadic) : false;
      const dst = this.nextTemp();
      this.emit(lowered.block, {
        op: "call_static",
        dst,
        callee,
        args: args.map((a) => a.value),
        variadic: isVariadic,
      });
      if (sig) return { value: dst, type: sig.retType, block: lowered.block };
      if (this.importedReturns && this.importedReturns.has(local)) {
        return { value: dst, type: parseTypeName(this.importedReturns.get(local)), block: lowered.block };
      }
      return { value: dst, type: { kind: "PrimitiveType", name: "unknown" }, block: lowered.block };
    }

    if (expr.callee.kind === "MemberExpr") {
      const method = expr.callee.name;
      if (expr.callee.target.kind === "Identifier" && this.prototypes.has(expr.callee.target.name)) {
        const protoName = expr.callee.target.name;
        if (method === "clone") {
          const dst = this.nextTemp();
          this.emit(block, { op: "call_static", dst, callee: `${protoName}.clone`, args: [], variadic: false });
          return { value: dst, type: { kind: "NamedType", name: protoName }, block };
        }
        const m = this.resolvePrototypeMethod(protoName, method);
        const lowered = lowerArgs(expr.args, block);
        const args = lowered.args;
        const dst = this.nextTemp();
        this.emit(lowered.block, {
          op: "call_static",
          dst,
          callee: `${protoName}.${method}`,
          args: args.map((a) => a.value),
          variadic: !!(m && m.params.some((p) => p.variadic)),
        });
        return { value: dst, type: m ? m.retType : { kind: "PrimitiveType", name: "unknown" }, block: lowered.block };
      }
      if (expr.callee.target.kind === "Identifier") {
        const ns = this.importNamespaces.get(expr.callee.target.name);
          if (ns) {
          let variadic = false;
          if (this.prototypes.has(ns)) {
            const m = this.resolvePrototypeMethod(ns, method);
            variadic = !!(m && m.params.some((p) => p.variadic));
          }
          const lowered = lowerArgs(expr.args, block);
          const args = lowered.args;
          const dst = this.nextTemp();
          this.emit(lowered.block, {
            op: "call_static",
            dst,
            callee: `${ns}.${method}`,
            args: args.map((a) => a.value),
            variadic,
          });
          if (this.moduleReturns && this.moduleReturns.has(ns)) {
            const ret = this.moduleReturns.get(ns).get(method);
            if (ret) return { value: dst, type: parseTypeName(ret), block: lowered.block };
          }
          return { value: dst, type: { kind: "PrimitiveType", name: "unknown" }, block: lowered.block };
        }
      }
      if (expr.callee.target.kind === "Identifier" && expr.callee.target.name === "Sys" && method === "print") {
        const lowered = lowerArgs(expr.args, block);
        const args = lowered.args;
        this.emit(lowered.block, { op: "call_builtin_print", args: args.map((a) => a.value) });
        return { value: this.nextTemp(), type: { kind: "PrimitiveType", name: "void" }, block: lowered.block };
      }
      let recv = null;
      let base = null;
      let memberName = null;
      if (expr.callee.target.kind === "MemberExpr") {
        const mem = expr.callee.target;
        if (mem.target.kind === "Identifier" && this.importNamespaces.has(mem.target.name)) {
          recv = this.lowerExpr(mem, block, irFn, scope);
        } else {
          base = this.lowerExpr(mem.target, block, irFn, scope);
          const dst = this.nextTemp();
          this.emit(base.block, { op: "member_get", dst, target: base.value, name: mem.name });
          recv = { value: dst, type: this.inferExprType(mem, scope), block: base.block };
          const baseType = this.inferExprType(mem.target, scope);
          if (baseType && baseType.kind === "NamedType" && this.prototypes.has(baseType.name)) {
            memberName = mem.name;
          }
        }
      } else {
        recv = this.lowerExpr(expr.callee.target, block, irFn, scope);
      }
      const lowered = lowerArgs(expr.args, recv.block);
      const args = lowered.args;
      if (recv.type && recv.type.kind === "NamedType") {
        const rt = recv.type.name;
        if (rt === "Dir") {
          const dst = this.nextTemp();
          const callee = method === "hasNext" ? "Fs.__dir_hasNext"
            : method === "next" ? "Fs.__dir_next"
            : method === "close" ? "Fs.__dir_close"
            : method === "reset" ? "Fs.__dir_reset"
            : null;
          if (callee) {
            this.emit(lowered.block, {
              op: "call_static",
              dst,
              callee,
              args: [recv.value, ...args.map((a) => a.value)],
              variadic: false,
            });
            if (method === "hasNext") return { value: dst, type: { kind: "PrimitiveType", name: "bool" }, block: lowered.block };
            if (method === "next") return { value: dst, type: { kind: "PrimitiveType", name: "string" }, block: lowered.block };
            return { value: dst, type: { kind: "PrimitiveType", name: "void" }, block: lowered.block };
          }
        }
        if (rt === "Walker") {
          const dst = this.nextTemp();
          const callee = method === "hasNext" ? "Fs.__walker_hasNext"
            : method === "next" ? "Fs.__walker_next"
            : method === "close" ? "Fs.__walker_close"
            : null;
          if (callee) {
            this.emit(lowered.block, {
              op: "call_static",
              dst,
              callee,
              args: [recv.value, ...args.map((a) => a.value)],
              variadic: false,
            });
            if (method === "hasNext") return { value: dst, type: { kind: "PrimitiveType", name: "bool" }, block: lowered.block };
            if (method === "next") return { value: dst, type: { kind: "NamedType", name: "PathEntry" }, block: lowered.block };
            return { value: dst, type: { kind: "PrimitiveType", name: "void" }, block: lowered.block };
          }
        }
      }
      if (recv.type && recv.type.kind === "NamedType" && this.prototypes.has(recv.type.name)) {
        const m = this.resolvePrototypeMethod(recv.type.name, method);
        const dst = this.nextTemp();
        this.emit(lowered.block, {
          op: "call_static",
          dst,
          callee: `${recv.type.name}.${method}`,
          args: [recv.value, ...args.map((a) => a.value)],
          variadic: !!(m && m.params.some((p) => p.variadic)),
        });
        return { value: dst, type: m ? m.retType : { kind: "PrimitiveType", name: "unknown" }, block: lowered.block };
      }
      if (method === "toString") {
        const dst = this.nextTemp();
        this.emit(recv.block, { op: "call_builtin_tostring", dst, value: recv.value });
        return { value: dst, type: { kind: "PrimitiveType", name: "string" }, block: recv.block };
      }
      if (method === "view" || method === "slice") {
        let offsetValue = null;
        let lenValue = null;
        if (args.length === 0) {
          const offset = this.nextTemp();
          this.emit(recv.block, { op: "const", dst: offset, literalType: "int", value: "0" });
          const lenTmp = this.nextTemp();
          this.emit(recv.block, { op: "call_method_static", dst: lenTmp, receiver: recv.value, method: "length", args: [] });
          offsetValue = offset;
          lenValue = lenTmp;
        } else {
          offsetValue = args[0].value;
          lenValue = args[1] ? args[1].value : null;
        }
        const dst = this.nextTemp();
        this.emit(lowered.block, { op: "check_view_bounds", target: recv.value, offset: offsetValue, len: lenValue });
        this.emit(lowered.block, {
          op: "make_view",
          dst,
          kind: method,
          source: recv.value,
          offset: offsetValue,
          len: lenValue,
          readonly: method === "view",
        });
        const elemType = this.elementTypeForIndex(recv.type);
        return { value: dst, type: { kind: "GenericType", name: method, args: [elemType] }, block: lowered.block };
      }
      const dst = this.nextTemp();
      this.emit(lowered.block, {
        op: "call_method_static",
        dst,
        receiver: recv.value,
        method,
        args: args.map((a) => a.value),
      });
      if (memberName && base && recv.type && recv.type.kind === "GenericType" && recv.type.name === "list") {
        this.emit(lowered.block, { op: "member_set", target: base.value, name: memberName, src: recv.value });
      }
      return { value: dst, type: { kind: "PrimitiveType", name: "unknown" }, block: lowered.block };
    }

    const dst = this.nextTemp();
    this.emit(block, { op: "call_unknown", dst });
    return { value: dst, type: { kind: "PrimitiveType", name: "unknown" }, block };
  }

  emitIndexChecks(block, target, index, opts = {}) {
    const forWrite = !!opts.forWrite;
    const ts = typeToString(target.type);
    if (ts.startsWith("map<")) {
      if (!forWrite) this.emit(block, { op: "check_map_has_key", map: target.value, key: index.value });
      return;
    }
    this.emit(block, { op: "check_index_bounds", target: target.value, index: index.value });
  }

  lowerIncDec(expr, block, irFn, scope, isPrefix, op) {
    const valueType = this.inferExprType(expr, scope);
    const typeName = typeToString(valueType);
    const literalType = typeName === "float" ? "float" : typeName === "byte" ? "byte" : "int";
    const literalValue = literalType === "float" ? "1.0" : "1";
    const operator = op === "++" ? "+" : "-";

    if (expr.kind === "Identifier") {
      const entry = scope.get(expr.name);
      const irName = entry ? entry.irName : expr.name;
      const cur = this.nextTemp();
      this.emit(block, { op: "load_var", dst: cur, name: irName, type: lowerType(valueType) });
      const one = this.nextTemp();
      this.emit(block, { op: "const", dst: one, literalType, value: literalValue });
      const next = this.nextTemp();
      this.emit(block, { op: "bin_op", dst: next, operator, left: cur, right: one });
      this.emit(block, { op: "store_var", name: irName, src: next, type: lowerType(valueType) });
      return { value: isPrefix ? next : cur, type: valueType, block };
    }

    if (expr.kind === "MemberExpr") {
      const base = this.lowerExpr(expr.target, block, irFn, scope);
      const cur = this.nextTemp();
      this.emit(base.block, { op: "member_get", dst: cur, target: base.value, name: expr.name });
      const one = this.nextTemp();
      this.emit(base.block, { op: "const", dst: one, literalType, value: literalValue });
      const next = this.nextTemp();
      this.emit(base.block, { op: "bin_op", dst: next, operator, left: cur, right: one });
      this.emit(base.block, { op: "member_set", target: base.value, name: expr.name, src: next });
      return { value: isPrefix ? next : cur, type: valueType, block: base.block };
    }

    if (expr.kind === "IndexExpr") {
      const target = this.lowerExpr(expr.target, block, irFn, scope);
      const index = this.lowerExpr(expr.index, target.block, irFn, scope);
      this.emitIndexChecks(index.block, target, index, { forWrite: false });
      const cur = this.nextTemp();
      this.emit(index.block, { op: "index_get", dst: cur, target: target.value, index: index.value });
      const one = this.nextTemp();
      this.emit(index.block, { op: "const", dst: one, literalType, value: literalValue });
      const next = this.nextTemp();
      this.emit(index.block, { op: "bin_op", dst: next, operator, left: cur, right: one });
      this.emit(index.block, { op: "index_set", target: target.value, index: index.value, src: next });
      return { value: isPrefix ? next : cur, type: valueType, block: index.block };
    }

    const v = this.lowerExpr(expr, block, irFn, scope);
    const dst = this.nextTemp();
    this.emit(v.block, { op: "unary_op", dst, operator: op, src: v.value });
    return { value: dst, type: v.type, block: v.block };
  }

  elementTypeForIndex(t) {
    if (!t || t.kind !== "GenericType") {
      if (typeToString(t) === "string") return { kind: "PrimitiveType", name: "glyph" };
      return { kind: "PrimitiveType", name: "unknown" };
    }
    if (["list", "slice", "view"].includes(t.name)) return t.args[0];
    if (t.name === "map") return t.args[1];
    return { kind: "PrimitiveType", name: "unknown" };
  }

  elementTypeForIter(t, forKind) {
    if (t && t.kind === "GenericType") {
      if (t.name === "map") return forKind === "in" ? t.args[0] : t.args[1];
      if (["list", "slice", "view"].includes(t.name)) return t.args[0];
    }
    if (typeToString(t) === "string") return { kind: "PrimitiveType", name: "glyph" };
    return { kind: "PrimitiveType", name: "unknown" };
  }

  inferExprType(expr, scope) {
    if (!expr) return { kind: "PrimitiveType", name: "void" };
    if (expr.kind === "Literal") return { kind: "PrimitiveType", name: expr.literalType };
    if (expr.kind === "CastExpr") return expr.targetType || { kind: "PrimitiveType", name: "unknown" };
    if (expr.kind === "Identifier") {
      const entry = scope.get(expr.name);
      return entry ? entry.type : { kind: "PrimitiveType", name: "unknown" };
    }
    if (expr.kind === "ListLiteral") {
      const e = expr.items.length > 0 ? this.inferExprType(expr.items[0], scope) : { kind: "PrimitiveType", name: "void" };
      return { kind: "GenericType", name: "list", args: [e] };
    }
    if (expr.kind === "MapLiteral") {
      const k = expr.pairs.length > 0 ? this.inferExprType(expr.pairs[0].key, scope) : { kind: "PrimitiveType", name: "void" };
      const v = expr.pairs.length > 0 ? this.inferExprType(expr.pairs[0].value, scope) : { kind: "PrimitiveType", name: "void" };
      return { kind: "GenericType", name: "map", args: [k, v] };
    }
    if (expr.kind === "CallExpr" && expr.callee.kind === "Identifier") {
      const sig = this.functions.get(expr.callee.name);
      return sig ? sig.retType : { kind: "PrimitiveType", name: "unknown" };
    }
    if (expr.kind === "CallExpr" && expr.callee.kind === "MemberExpr") {
      const m = expr.callee;
      if (m.target.kind === "Identifier") {
        const ns = this.importNamespaces.get(m.target.name);
        if (ns) {
          const ft = this.inferFileTypeFromIoOpen(expr);
          if (ft) return { kind: "NamedType", name: ft };
        }
      }
      if (m.target.kind === "Identifier" && this.prototypes.has(m.target.name)) {
        if (m.name === "clone") return { kind: "NamedType", name: m.target.name };
        const mm = this.resolvePrototypeMethod(m.target.name, m.name);
        return mm ? mm.retType : { kind: "PrimitiveType", name: "unknown" };
      }
      const t = this.inferExprType(m.target, scope);
      if (t && t.kind === "NamedType" && this.prototypes.has(t.name)) {
        const mm = this.resolvePrototypeMethod(t.name, m.name);
        return mm ? mm.retType : { kind: "PrimitiveType", name: "unknown" };
      }
      if (t && t.kind === "NamedType" && (t.name === "TextFile" || t.name === "BinaryFile")) {
        if (m.name === "read") {
          if (t.name === "BinaryFile") {
            return { kind: "GenericType", name: "list", args: [{ kind: "PrimitiveType", name: "byte" }] };
          }
          return { kind: "PrimitiveType", name: "string" };
        }
        if (m.name === "write" || m.name === "close" || m.name === "seek") return { kind: "PrimitiveType", name: "void" };
        if (m.name === "tell" || m.name === "size") return { kind: "PrimitiveType", name: "int" };
        if (m.name === "name") return { kind: "PrimitiveType", name: "string" };
      }
    }
    if (expr.kind === "MemberExpr") {
      const t = this.inferExprType(expr.target, scope);
      if (t && t.kind === "NamedType" && this.prototypes.has(t.name)) {
        return this.resolvePrototypeField(t.name, expr.name) || { kind: "PrimitiveType", name: "unknown" };
      }
    }
    return { kind: "PrimitiveType", name: "unknown" };
  }

  inferAssignableType(target, scope) {
    if (!target) return { kind: "PrimitiveType", name: "unknown" };
    if (target.kind === "Identifier") {
      const entry = scope.get(target.name);
      return entry ? entry.type : { kind: "PrimitiveType", name: "unknown" };
    }
    if (target.kind === "MemberExpr") {
      const t = this.inferExprType(target.target, scope);
      if (t && t.kind === "NamedType" && this.prototypes.has(t.name)) {
        return this.resolvePrototypeField(t.name, target.name) || { kind: "PrimitiveType", name: "unknown" };
      }
      return { kind: "PrimitiveType", name: "unknown" };
    }
    if (target.kind === "IndexExpr") {
      const t = this.inferExprType(target.target, scope);
      return this.elementTypeForIndex(t);
    }
    return { kind: "PrimitiveType", name: "unknown" };
  }
}

function printIR(ir) {
  const out = [];
  out.push("module ProtoScript");
  for (const fn of ir.functions) {
    const params = fn.params
      .map((p) => `${p.name}:${p.type.name}${p.variadic ? "..." : ""}`)
      .join(", ");
    out.push(`function ${fn.name}(${params}) -> ${fn.returnType.name}`);
    for (const b of fn.blocks) {
      out.push(`  block ${b.label}`);
      for (const ins of b.instrs) {
        out.push(`    ${formatInstr(ins)}`);
      }
    }
  }
  return `${out.join("\n")}\n`;
}

function formatInstr(i) {
  switch (i.op) {
    case "const":
      return `${i.dst} = const ${i.literalType} ${JSON.stringify(i.value)}`;
    case "var_decl":
      return `var ${i.name}:${i.type.name}${i.type.repr ? ` ${i.type.repr}` : ""}`;
    case "load_var":
      return `${i.dst} = load ${i.name}:${i.type.name}`;
    case "store_var":
      return `store ${i.name} <- ${i.src}`;
    case "bin_op":
      return `${i.dst} = ${i.left} ${i.operator} ${i.right}`;
    case "unary_op":
      return `${i.dst} = ${i.operator}${i.src}`;
    case "copy":
      return `${i.dst} = copy ${i.src}`;
    case "postfix_op":
      return `${i.dst} = ${i.src}${i.operator}`;
    case "call_static":
      return `${i.dst} = call_static ${i.callee}(${i.args.join(", ")})`;
    case "call_method_static":
      return `${i.dst} = call_method_static ${i.receiver}.${i.method}(${i.args.join(", ")})`;
    case "call_builtin_print":
      return `call_builtin_print(${i.args.join(", ")})`;
    case "call_builtin_tostring":
      return `${i.dst} = call_builtin_tostring(${i.value})`;
    case "push_handler":
      return `push_handler ${i.target}`;
    case "pop_handler":
      return "pop_handler";
    case "get_exception":
      return `${i.dst} = get_exception`;
    case "exception_is":
      return `${i.dst} = exception_is ${i.value} ${i.type}`;
    case "rethrow":
      return "rethrow";
    case "make_view":
      return `${i.dst} = make_${i.kind}(source=${i.source}, offset=${i.offset}, len=${i.len}, readonly=${i.readonly})`;
    case "index_get":
      return `${i.dst} = index_get ${i.target}[${i.index}]`;
    case "index_set":
      return `index_set ${i.target}[${i.index}] <- ${i.src}`;
    case "check_int_overflow":
      return `check_int_overflow ${i.left} ${i.operator} ${i.right}`;
    case "check_int_overflow_unary_minus":
      return `check_int_overflow unary - ${i.value}`;
    case "check_div_zero":
      return `check_div_zero ${i.divisor}`;
    case "check_shift_range":
      return `check_shift_range shift=${i.shift} width=${i.width}`;
    case "check_index_bounds":
      return `check_index_bounds target=${i.target} index=${i.index}`;
    case "check_view_bounds":
      return `check_view_bounds target=${i.target} offset=${i.offset} len=${i.len}`;
    case "check_map_has_key":
      return `check_map_has_key map=${i.map} key=${i.key}`;
    case "ret":
      return `ret ${i.value}`;
    case "ret_void":
      return "ret_void";
    case "throw":
      return `throw ${i.value}`;
    case "branch_if":
      return `branch_if ${i.cond} then ${i.then} else ${i.else}`;
    case "jump":
      return `jump ${i.target}`;
    case "iter_begin":
      return `${i.dst} = iter_begin ${i.source} mode=${i.mode}`;
    case "branch_iter_has_next":
      return `branch_iter_has_next ${i.iter} then ${i.then} else ${i.else}`;
    case "iter_next":
      return `${i.dst} = iter_next ${i.iter} mode=${i.mode}`;
    case "select":
      return `${i.dst} = select ${i.cond} ? ${i.thenValue} : ${i.elseValue}`;
    case "make_list":
      return `${i.dst} = make_list [${i.items.join(", ")}]`;
    case "make_map":
      return `${i.dst} = make_map {${i.pairs.map((p) => `${p.key}:${p.value}`).join(", ")}}`;
    case "member_get":
      return `${i.dst} = member_get ${i.target}.${i.name}`;
    case "member_set":
      return `member_set ${i.target}.${i.name} <- ${i.src}`;
    case "make_object":
      return `${i.dst} = make_object ${i.proto || ""}`.trim();
    case "nop":
      return "nop";
    default:
      return `${i.op} ${JSON.stringify(i)}`;
  }
}

function buildIR(ast) {
  return new IRBuilder(ast).build();
}

const IR_VERSION = "1.0.0";

function serializeIR(moduleIR) {
  return {
    ir_version: IR_VERSION,
    format: "ProtoScriptIR",
    module: moduleIR,
  };
}

function validateSerializedIR(doc) {
  const errors = [];
  const err = (msg) => errors.push(msg);

  if (!doc || typeof doc !== "object") return ["root must be an object"];
  if (doc.format !== "ProtoScriptIR") err("format must be 'ProtoScriptIR'");
  if (doc.ir_version !== IR_VERSION) err(`ir_version must be '${IR_VERSION}'`);
  if (!doc.module || typeof doc.module !== "object") err("module must be an object");
  if (errors.length > 0) return errors;

  const isNonEmptyString = (v) => typeof v === "string" && v.length > 0;
  const isIRType = (t) => t && typeof t === "object" && t.kind === "IRType" && isNonEmptyString(t.name);
  const isIdent = (s) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(s);
  const splitTypeArgs = (inner) => {
    const out = [];
    let depth = 0;
    let start = 0;
    for (let i = 0; i < inner.length; i += 1) {
      const ch = inner[i];
      if (ch === "<") depth += 1;
      else if (ch === ">") depth -= 1;
      else if (ch === "," && depth === 0) {
        out.push(inner.slice(start, i).trim());
        start = i + 1;
      }
    }
    out.push(inner.slice(start).trim());
    return out.filter((p) => p.length > 0);
  };
  const isValidTypeName = (name) => {
    if (!isNonEmptyString(name)) return false;
    if (!name.includes("<")) return isIdent(name);
    const m = /^([A-Za-z_][A-Za-z0-9_]*)<(.*)>$/.exec(name);
    if (!m) return false;
    const kind = m[1];
    const inner = m[2];
    if (!["list", "view", "slice", "map"].includes(kind)) return false;
    const args = splitTypeArgs(inner);
    if (kind === "map") {
      if (args.length !== 2) return false;
      return args.every(isValidTypeName);
    }
    if (args.length !== 1) return false;
    return isValidTypeName(args[0]);
  };

  const m = doc.module;
  if (m.kind !== "Module") err("module.kind must be 'Module'");
  if (!Array.isArray(m.functions)) err("module.functions must be an array");
  if (errors.length > 0) return errors;

  const knownOps = new Set([
    "var_decl",
    "load_var",
    "store_var",
    "const",
    "copy",
    "unary_op",
    "bin_op",
    "postfix_op",
    "call_static",
    "call_method_static",
    "call_unknown",
    "call_builtin_print",
    "call_builtin_tostring",
    "push_handler",
    "pop_handler",
    "get_exception",
    "exception_is",
    "rethrow",
    "make_view",
    "index_get",
    "index_set",
    "check_int_overflow",
    "check_int_overflow_unary_minus",
    "check_div_zero",
    "check_shift_range",
    "check_index_bounds",
    "check_view_bounds",
    "check_map_has_key",
    "ret",
    "ret_void",
    "throw",
    "branch_if",
    "jump",
    "iter_begin",
    "branch_iter_has_next",
    "iter_next",
    "select",
    "make_list",
    "make_map",
    "make_object",
    "member_get",
    "member_set",
    "break",
    "continue",
    "nop",
    "unknown_expr",
    "unhandled_stmt",
  ]);

  for (let fi = 0; fi < m.functions.length; fi += 1) {
    const fn = m.functions[fi];
    if (!fn || typeof fn !== "object") {
      err(`functions[${fi}] must be an object`);
      continue;
    }
    if (fn.kind !== "Function") err(`functions[${fi}].kind must be 'Function'`);
    if (!isNonEmptyString(fn.name)) err(`functions[${fi}].name must be non-empty string`);
    if (!isIRType(fn.returnType) || !isValidTypeName(fn.returnType.name)) {
      err(`functions[${fi}].returnType must be IRType`);
    }
    if (!Array.isArray(fn.params)) err(`functions[${fi}].params must be array`);
    if (!Array.isArray(fn.blocks) || fn.blocks.length === 0) err(`functions[${fi}].blocks must be non-empty array`);
    if (!Array.isArray(fn.blocks) || fn.blocks.length === 0) continue;

    const paramNames = new Set();
    for (let pi = 0; pi < fn.params.length; pi += 1) {
      const p = fn.params[pi];
      if (!p || typeof p !== "object") {
        err(`functions[${fi}].params[${pi}] must be object`);
        continue;
      }
      if (!isNonEmptyString(p.name)) err(`functions[${fi}].params[${pi}].name must be non-empty string`);
      if (paramNames.has(p.name)) err(`functions[${fi}] duplicated param name '${p.name}'`);
      paramNames.add(p.name);
      if (!isIRType(p.type) || !isValidTypeName(p.type.name)) {
        err(`functions[${fi}].params[${pi}].type must be IRType`);
      }
      if (p.variadic !== undefined && typeof p.variadic !== "boolean") {
        err(`functions[${fi}].params[${pi}].variadic must be boolean`);
      }
    }

    const labels = new Set();
    for (let bi = 0; bi < fn.blocks.length; bi += 1) {
      const b = fn.blocks[bi];
      if (!b || typeof b !== "object") {
        err(`functions[${fi}].blocks[${bi}] must be object`);
        continue;
      }
      if (b.kind !== "Block") err(`functions[${fi}].blocks[${bi}].kind must be 'Block'`);
      if (!isNonEmptyString(b.label)) err(`functions[${fi}].blocks[${bi}].label must be non-empty string`);
      if (labels.has(b.label)) err(`functions[${fi}] duplicated block label '${b.label}'`);
      labels.add(b.label);
      if (!Array.isArray(b.instrs)) {
        err(`functions[${fi}].blocks[${bi}].instrs must be array`);
        continue;
      }
      if (b.instrs.length === 0) err(`functions[${fi}].blocks[${bi}] must contain at least one instruction`);
      for (let ii = 0; ii < b.instrs.length; ii += 1) {
        const ins = b.instrs[ii];
        if (!ins || typeof ins !== "object") {
          err(`functions[${fi}].blocks[${bi}].instrs[${ii}] must be object`);
          continue;
        }
        if (!isNonEmptyString(ins.op)) {
          err(`functions[${fi}].blocks[${bi}].instrs[${ii}].op must be non-empty string`);
          continue;
        }
        if (!knownOps.has(ins.op)) err(`functions[${fi}].blocks[${bi}].instrs[${ii}].op unknown '${ins.op}'`);
        const requireFields = (fields) => {
          for (const f of fields) {
            if (!(f in ins)) err(`functions[${fi}].blocks[${bi}].instrs[${ii}] missing '${f}'`);
          }
        };
        switch (ins.op) {
          case "var_decl":
            requireFields(["name", "type"]);
            if (ins.type && (!isIRType(ins.type) || !isValidTypeName(ins.type.name))) {
              err(`functions[${fi}].blocks[${bi}].instrs[${ii}] invalid IRType`);
            }
            break;
          case "load_var":
            requireFields(["name", "type", "dst"]);
            if (ins.type && (!isIRType(ins.type) || !isValidTypeName(ins.type.name))) {
              err(`functions[${fi}].blocks[${bi}].instrs[${ii}] invalid IRType`);
            }
            break;
          case "store_var":
            requireFields(["name", "src"]);
            break;
          case "const":
            requireFields(["dst", "literalType"]);
            break;
          case "copy":
            requireFields(["dst", "src"]);
            break;
          case "unary_op":
            requireFields(["dst", "src", "operator"]);
            break;
          case "bin_op":
            requireFields(["dst", "left", "right", "operator"]);
            break;
          case "postfix_op":
            requireFields(["dst", "target", "operator"]);
            break;
          case "call_static":
            requireFields(["dst", "callee", "args"]);
            break;
          case "call_method_static":
            requireFields(["dst", "receiver", "method", "args"]);
            break;
          case "call_unknown":
            requireFields(["dst", "callee", "args"]);
            break;
          case "call_builtin_print":
            requireFields(["args"]);
            break;
          case "call_builtin_tostring":
            requireFields(["dst", "value"]);
            break;
          case "push_handler":
            requireFields(["target"]);
            break;
          case "get_exception":
            requireFields(["dst"]);
            break;
          case "exception_is":
            requireFields(["dst", "value", "type"]);
            break;
          case "make_view":
            requireFields(["dst", "source", "offset", "len", "readonly"]);
            break;
          case "index_get":
            requireFields(["dst", "target", "index"]);
            break;
          case "index_set":
            requireFields(["target", "index", "src"]);
            break;
          case "check_int_overflow":
            requireFields(["left", "right", "operator"]);
            break;
          case "check_int_overflow_unary_minus":
            requireFields(["value"]);
            break;
          case "check_div_zero":
            requireFields(["divisor"]);
            break;
          case "check_shift_range":
            requireFields(["shift", "width"]);
            break;
          case "check_index_bounds":
            requireFields(["target", "index"]);
            break;
          case "check_view_bounds":
            requireFields(["target", "offset", "len"]);
            break;
          case "check_map_has_key":
            requireFields(["map", "key"]);
            break;
          case "ret":
            requireFields(["value"]);
            break;
          case "throw":
            requireFields(["value"]);
            break;
          case "branch_if":
            requireFields(["cond", "then", "else"]);
            break;
          case "jump":
            requireFields(["target"]);
            break;
          case "iter_begin":
            requireFields(["dst", "source", "mode"]);
            break;
          case "branch_iter_has_next":
            requireFields(["iter", "then", "else"]);
            break;
          case "iter_next":
            requireFields(["dst", "iter", "source", "mode"]);
            break;
          case "select":
            requireFields(["dst", "cond", "thenValue", "elseValue"]);
            break;
          case "make_list":
            requireFields(["dst", "items"]);
            break;
          case "make_map":
            requireFields(["dst", "pairs"]);
            break;
          case "make_object":
            requireFields(["dst", "proto"]);
            break;
          case "member_get":
            requireFields(["dst", "target", "name"]);
            break;
          case "member_set":
            requireFields(["target", "name", "src"]);
            break;
          default:
            break;
        }
      }
    }

    if (!labels.has("entry")) err(`functions[${fi}] missing entry block`);

    // Validate branch/jump targets exist.
    for (let bi = 0; bi < fn.blocks.length; bi += 1) {
      const b = fn.blocks[bi];
      for (let ii = 0; ii < b.instrs.length; ii += 1) {
        const ins = b.instrs[ii];
        if (ins.op === "jump" && typeof ins.target === "string" && !labels.has(ins.target)) {
          err(`functions[${fi}] jump target '${ins.target}' does not exist`);
        }
        if (ins.op === "branch_if") {
          if (typeof ins.then === "string" && !labels.has(ins.then)) err(`functions[${fi}] branch_if then target '${ins.then}' does not exist`);
          if (typeof ins.else === "string" && !labels.has(ins.else)) err(`functions[${fi}] branch_if else target '${ins.else}' does not exist`);
        }
        if (ins.op === "branch_iter_has_next") {
          if (typeof ins.then === "string" && !labels.has(ins.then)) err(`functions[${fi}] branch_iter_has_next then target '${ins.then}' does not exist`);
          if (typeof ins.else === "string" && !labels.has(ins.else)) err(`functions[${fi}] branch_iter_has_next else target '${ins.else}' does not exist`);
        }
      }
    }
  }

  return errors;
}

module.exports = {
  IR_VERSION,
  buildIR,
  printIR,
  serializeIR,
  validateSerializedIR,
};
