"use strict";

function typeToString(t) {
  if (!t) return "unknown";
  if (typeof t === "string") return t;
  if (t.kind === "PrimitiveType" || t.kind === "NamedType") return t.name;
  if (t.kind === "GenericType") return `${t.name}<${t.args.map(typeToString).join(",")}>`;
  if (t.kind === "IRType") return t.name;
  return "unknown";
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

class Scope {
  constructor(parent = null) {
    this.parent = parent;
    this.vars = new Map();
  }
  define(name, t) {
    this.vars.set(name, t);
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
    this.functions = new Map();
  }

  nextTemp() {
    this.tempId += 1;
    return `%t${this.tempId}`;
  }

  nextBlock(prefix = "b") {
    this.blockId += 1;
    return `${prefix}${this.blockId}`;
  }

  emit(block, instr) {
    block.instrs.push(instr);
  }

  build() {
    const mod = { kind: "Module", functions: [] };
    for (const d of this.ast.decls) {
      if (d.kind === "FunctionDecl") this.functions.set(d.name, d);
    }
    for (const d of this.ast.decls) {
      if (d.kind === "FunctionDecl") mod.functions.push(this.lowerFunction(d));
    }
    return mod;
  }

  lowerFunction(fn) {
    const entry = { kind: "Block", label: "entry", instrs: [] };
    const irFn = {
      kind: "Function",
      name: fn.name,
      params: fn.params.map((p) => ({ name: p.name, type: lowerType(p.type), variadic: !!p.variadic })),
      returnType: lowerType(fn.retType),
      blocks: [entry],
    };
    const scope = new Scope(null);
    for (const p of fn.params) scope.define(p.name, p.type);
    this.lowerBlock(fn.body, entry, irFn, scope);
    return irFn;
  }

  lowerBlock(blockAst, block, irFn, scope) {
    const local = new Scope(scope);
    for (const s of blockAst.stmts) this.lowerStmt(s, block, irFn, local);
  }

  lowerStmt(stmt, block, irFn, scope) {
    switch (stmt.kind) {
      case "VarDecl": {
        const declared = stmt.declaredType || (stmt.init ? this.inferExprType(stmt.init, scope) : { kind: "PrimitiveType", name: "void" });
        scope.define(stmt.name, declared);
        this.emit(block, { op: "var_decl", name: stmt.name, type: lowerType(declared) });
        if (stmt.init) {
          const rhs = this.lowerExpr(stmt.init, block, irFn, scope);
          this.emit(block, { op: "store_var", name: stmt.name, src: rhs.value, type: lowerType(rhs.type) });
        }
        break;
      }
      case "AssignStmt": {
        const rhs = this.lowerExpr(stmt.expr, block, irFn, scope);
        if (stmt.target.kind === "Identifier") {
          this.emit(block, { op: "store_var", name: stmt.target.name, src: rhs.value, type: lowerType(rhs.type) });
        } else if (stmt.target.kind === "IndexExpr") {
          const t = this.lowerExpr(stmt.target.target, block, irFn, scope);
          const i = this.lowerExpr(stmt.target.index, block, irFn, scope);
          this.emitIndexChecks(block, t, i);
          this.emit(block, { op: "index_set", target: t.value, index: i.value, src: rhs.value });
        }
        break;
      }
      case "ExprStmt":
        this.lowerExpr(stmt.expr, block, irFn, scope);
        break;
      case "ReturnStmt":
        if (stmt.expr) {
          const v = this.lowerExpr(stmt.expr, block, irFn, scope);
          this.emit(block, { op: "ret", value: v.value, type: lowerType(v.type) });
        } else {
          this.emit(block, { op: "ret_void" });
        }
        break;
      case "ThrowStmt": {
        const v = this.lowerExpr(stmt.expr, block, irFn, scope);
        this.emit(block, { op: "throw", value: v.value });
        break;
      }
      case "ForStmt":
        this.lowerFor(stmt, block, irFn, scope);
        break;
      case "SwitchStmt":
        this.lowerSwitch(stmt, block, irFn, scope);
        break;
      case "Block":
        this.lowerBlock(stmt, block, irFn, scope);
        break;
      case "BreakStmt":
        this.emit(block, { op: "break" });
        break;
      case "ContinueStmt":
        this.emit(block, { op: "continue" });
        break;
      default:
        this.emit(block, { op: "unhandled_stmt", kind: stmt.kind });
        break;
    }
  }

  lowerFor(stmt, block, irFn, scope) {
    if (stmt.forKind === "classic") {
      const init = this.nextBlock("for_init_");
      const cond = this.nextBlock("for_cond_");
      const body = this.nextBlock("for_body_");
      const step = this.nextBlock("for_step_");
      const done = this.nextBlock("for_done_");
      const bInit = { kind: "Block", label: init, instrs: [] };
      const bCond = { kind: "Block", label: cond, instrs: [] };
      const bBody = { kind: "Block", label: body, instrs: [] };
      const bStep = { kind: "Block", label: step, instrs: [] };
      const bDone = { kind: "Block", label: done, instrs: [] };
      irFn.blocks.push(bInit, bCond, bBody, bStep, bDone);
      this.emit(block, { op: "jump", target: init });
      if (stmt.init) this.lowerStmtLikeForPart(stmt.init, bInit, irFn, new Scope(scope));
      this.emit(bInit, { op: "jump", target: cond });
      if (stmt.cond) {
        const c = this.lowerExpr(stmt.cond, bCond, irFn, scope);
        this.emit(bCond, { op: "branch_if", cond: c.value, then: body, else: done });
      } else {
        this.emit(bCond, { op: "jump", target: body });
      }
      this.lowerStmt(stmt.body, bBody, irFn, new Scope(scope));
      this.emit(bBody, { op: "jump", target: step });
      if (stmt.step) this.lowerStmtLikeForPart(stmt.step, bStep, irFn, new Scope(scope));
      this.emit(bStep, { op: "jump", target: cond });
      this.emit(bDone, { op: "nop" });
      return;
    }

    const init = this.nextBlock(`for_${stmt.forKind}_init_`);
    const cond = this.nextBlock(`for_${stmt.forKind}_cond_`);
    const body = this.nextBlock(`for_${stmt.forKind}_body_`);
    const done = this.nextBlock(`for_${stmt.forKind}_done_`);
    const bInit = { kind: "Block", label: init, instrs: [] };
    const bCond = { kind: "Block", label: cond, instrs: [] };
    const bBody = { kind: "Block", label: body, instrs: [] };
    const bDone = { kind: "Block", label: done, instrs: [] };
    irFn.blocks.push(bInit, bCond, bBody, bDone);

    const seq = this.lowerExpr(stmt.iterExpr, bInit, irFn, scope);
    const cursor = this.nextTemp();
    this.emit(block, { op: "jump", target: init });
    this.emit(bInit, { op: "iter_begin", dst: cursor, source: seq.value, mode: stmt.forKind });
    this.emit(bInit, { op: "jump", target: cond });
    this.emit(bCond, { op: "branch_iter_has_next", iter: cursor, then: body, else: done });

    const sBody = new Scope(scope);
    const elem = this.nextTemp();
    this.emit(bBody, { op: "iter_next", dst: elem, iter: cursor, source: seq.value, mode: stmt.forKind });
    if (stmt.iterVar) {
      const vt = stmt.iterVar.declaredType || this.elementTypeForIter(seq.type, stmt.forKind);
      sBody.define(stmt.iterVar.name, vt);
      this.emit(bBody, { op: "var_decl", name: stmt.iterVar.name, type: lowerType(vt) });
      this.emit(bBody, { op: "store_var", name: stmt.iterVar.name, src: elem, type: lowerType(vt) });
    }
    this.lowerStmt(stmt.body, bBody, irFn, sBody);
    this.emit(bBody, { op: "jump", target: cond });
    this.emit(bDone, { op: "nop" });
  }

  lowerStmtLikeForPart(node, block, irFn, scope) {
    if (node.kind === "VarDecl" || node.kind === "AssignStmt") this.lowerStmt(node, block, irFn, scope);
    else this.lowerExpr(node, block, irFn, scope);
  }

  lowerSwitch(stmt, block, irFn, scope) {
    const value = this.lowerExpr(stmt.expr, block, irFn, scope);
    const done = this.nextBlock("switch_done_");
    const doneBlock = { kind: "Block", label: done, instrs: [] };
    irFn.blocks.push(doneBlock);

    let nextLabel = null;
    for (let idx = 0; idx < stmt.cases.length; idx += 1) {
      const c = stmt.cases[idx];
      const cmpLabel = nextLabel || this.nextBlock("switch_casecmp_");
      const bodyLabel = this.nextBlock("switch_casebody_");
      nextLabel = this.nextBlock("switch_casecmp_");
      const bCmp = { kind: "Block", label: cmpLabel, instrs: [] };
      const bBody = { kind: "Block", label: bodyLabel, instrs: [] };
      irFn.blocks.push(bCmp, bBody);
      if (idx === 0) this.emit(block, { op: "jump", target: cmpLabel });
      const cv = this.lowerExpr(c.value, bCmp, irFn, scope);
      const eq = this.nextTemp();
      this.emit(bCmp, { op: "bin_op", dst: eq, operator: "==", left: value.value, right: cv.value });
      this.emit(bCmp, { op: "branch_if", cond: eq, then: bodyLabel, else: nextLabel });
      const sBody = new Scope(scope);
      for (const st of c.stmts) this.lowerStmt(st, bBody, irFn, sBody);
      this.emit(bBody, { op: "jump", target: done });
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
      for (const st of stmt.defaultCase.stmts) this.lowerStmt(st, bDefault, irFn, new Scope(scope));
      this.emit(bDefault, { op: "jump", target: done });
    }
    this.emit(doneBlock, { op: "nop" });
  }

  lowerExpr(expr, block, irFn, scope) {
    switch (expr.kind) {
      case "Literal": {
        const dst = this.nextTemp();
        const t = { kind: "PrimitiveType", name: expr.literalType };
        this.emit(block, { op: "const", dst, literalType: expr.literalType, value: expr.value });
        return { value: dst, type: t };
      }
      case "Identifier": {
        const dst = this.nextTemp();
        const t = scope.get(expr.name) || { kind: "PrimitiveType", name: "unknown" };
        this.emit(block, { op: "load_var", dst, name: expr.name, type: lowerType(t) });
        return { value: dst, type: t };
      }
      case "UnaryExpr": {
        const v = this.lowerExpr(expr.expr, block, irFn, scope);
        const dst = this.nextTemp();
        if (expr.op === "-" && isIntLike(v.type)) {
          this.emit(block, { op: "check_int_overflow_unary_minus", value: v.value });
        }
        this.emit(block, { op: "unary_op", dst, operator: expr.op, src: v.value });
        return { value: dst, type: v.type };
      }
      case "BinaryExpr": {
        const l = this.lowerExpr(expr.left, block, irFn, scope);
        const r = this.lowerExpr(expr.right, block, irFn, scope);
        if (["+", "-", "*"].includes(expr.op) && isIntLike(l.type) && isIntLike(r.type)) {
          this.emit(block, { op: "check_int_overflow", operator: expr.op, left: l.value, right: r.value });
        }
        if (["/", "%"].includes(expr.op) && isIntLike(l.type) && isIntLike(r.type)) {
          this.emit(block, { op: "check_div_zero", divisor: r.value });
        }
        if (["<<", ">>"].includes(expr.op) && isIntLike(l.type) && isIntLike(r.type)) {
          this.emit(block, { op: "check_shift_range", shift: r.value, width: typeToString(l.type) === "byte" ? 8 : 64 });
        }
        const dst = this.nextTemp();
        this.emit(block, { op: "bin_op", dst, operator: expr.op, left: l.value, right: r.value });
        const outType = ["==", "!=", "<", "<=", ">", ">=", "&&", "||"].includes(expr.op)
          ? { kind: "PrimitiveType", name: "bool" }
          : l.type;
        return { value: dst, type: outType };
      }
      case "ConditionalExpr": {
        const c = this.lowerExpr(expr.cond, block, irFn, scope);
        const thenV = this.lowerExpr(expr.thenExpr, block, irFn, scope);
        const elseV = this.lowerExpr(expr.elseExpr, block, irFn, scope);
        const dst = this.nextTemp();
        this.emit(block, {
          op: "select",
          dst,
          cond: c.value,
          thenValue: thenV.value,
          elseValue: elseV.value,
        });
        return { value: dst, type: thenV.type };
      }
      case "CallExpr":
        return this.lowerCallExpr(expr, block, irFn, scope);
      case "IndexExpr": {
        const t = this.lowerExpr(expr.target, block, irFn, scope);
        const i = this.lowerExpr(expr.index, block, irFn, scope);
        this.emitIndexChecks(block, t, i);
        const dst = this.nextTemp();
        this.emit(block, { op: "index_get", dst, target: t.value, index: i.value });
        return { value: dst, type: this.elementTypeForIndex(t.type) };
      }
      case "MemberExpr": {
        const base = this.lowerExpr(expr.target, block, irFn, scope);
        const dst = this.nextTemp();
        this.emit(block, { op: "member_get", dst, target: base.value, name: expr.name });
        return { value: dst, type: { kind: "PrimitiveType", name: "unknown" } };
      }
      case "PostfixExpr": {
        const v = this.lowerExpr(expr.expr, block, irFn, scope);
        const dst = this.nextTemp();
        this.emit(block, { op: "postfix_op", dst, operator: expr.op, src: v.value });
        return { value: dst, type: v.type };
      }
      case "ListLiteral": {
        const dst = this.nextTemp();
        const vals = expr.items.map((it) => this.lowerExpr(it, block, irFn, scope));
        this.emit(block, { op: "make_list", dst, items: vals.map((v) => v.value) });
        const elemType = vals.length > 0 ? vals[0].type : { kind: "PrimitiveType", name: "void" };
        return { value: dst, type: { kind: "GenericType", name: "list", args: [elemType] } };
      }
      case "MapLiteral": {
        const dst = this.nextTemp();
        const pairs = expr.pairs.map((p) => ({
          key: this.lowerExpr(p.key, block, irFn, scope),
          value: this.lowerExpr(p.value, block, irFn, scope),
        }));
        this.emit(block, { op: "make_map", dst, pairs: pairs.map((p) => ({ key: p.key.value, value: p.value.value })) });
        const keyType = pairs.length > 0 ? pairs[0].key.type : { kind: "PrimitiveType", name: "void" };
        const valType = pairs.length > 0 ? pairs[0].value.type : { kind: "PrimitiveType", name: "void" };
        return { value: dst, type: { kind: "GenericType", name: "map", args: [keyType, valType] } };
      }
      default: {
        const dst = this.nextTemp();
        this.emit(block, { op: "unknown_expr", dst, kind: expr.kind });
        return { value: dst, type: { kind: "PrimitiveType", name: "unknown" } };
      }
    }
  }

  lowerCallExpr(expr, block, irFn, scope) {
    if (expr.callee.kind === "Identifier") {
      const callee = expr.callee.name;
      const args = expr.args.map((a) => this.lowerExpr(a, block, irFn, scope));
      const sig = this.functions.get(callee);
      const isVariadic = sig ? sig.params.some((p) => p.variadic) : false;
      const dst = this.nextTemp();
      this.emit(block, {
        op: "call_static",
        dst,
        callee,
        args: args.map((a) => a.value),
        variadic: isVariadic,
      });
      return { value: dst, type: sig ? sig.retType : { kind: "PrimitiveType", name: "unknown" } };
    }

    if (expr.callee.kind === "MemberExpr") {
      const method = expr.callee.name;
      if (expr.callee.target.kind === "Identifier" && expr.callee.target.name === "Sys" && method === "print") {
        const args = expr.args.map((a) => this.lowerExpr(a, block, irFn, scope));
        this.emit(block, { op: "call_builtin_print", args: args.map((a) => a.value) });
        return { value: this.nextTemp(), type: { kind: "PrimitiveType", name: "void" } };
      }
      const recv = this.lowerExpr(expr.callee.target, block, irFn, scope);
      const args = expr.args.map((a) => this.lowerExpr(a, block, irFn, scope));
      if (method === "toString") {
        const dst = this.nextTemp();
        this.emit(block, { op: "call_builtin_tostring", dst, value: recv.value });
        return { value: dst, type: { kind: "PrimitiveType", name: "string" } };
      }
      if (method === "view" || method === "slice") {
        const dst = this.nextTemp();
        const len = args.length >= 2 ? args[1].value : `len(${recv.value})`;
        this.emit(block, {
          op: "make_view",
          dst,
          kind: method,
          source: recv.value,
          len,
          readonly: method === "view",
        });
        const elemType = this.elementTypeForIndex(recv.type);
        return { value: dst, type: { kind: "GenericType", name: method, args: [elemType] } };
      }
      const dst = this.nextTemp();
      this.emit(block, {
        op: "call_method_static",
        dst,
        receiver: recv.value,
        method,
        args: args.map((a) => a.value),
      });
      return { value: dst, type: { kind: "PrimitiveType", name: "unknown" } };
    }

    const dst = this.nextTemp();
    this.emit(block, { op: "call_unknown", dst });
    return { value: dst, type: { kind: "PrimitiveType", name: "unknown" } };
  }

  emitIndexChecks(block, target, index) {
    const ts = typeToString(target.type);
    if (ts.startsWith("map<")) {
      this.emit(block, { op: "check_map_has_key", map: target.value, key: index.value });
      return;
    }
    this.emit(block, { op: "check_index_bounds", target: target.value, index: index.value });
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
    if (expr.kind === "Identifier") return scope.get(expr.name) || { kind: "PrimitiveType", name: "unknown" };
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
    case "make_view":
      return `${i.dst} = make_${i.kind}(source=${i.source}, len=${i.len}, readonly=${i.readonly})`;
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
    "make_view",
    "index_get",
    "index_set",
    "check_int_overflow",
    "check_int_overflow_unary_minus",
    "check_div_zero",
    "check_shift_range",
    "check_index_bounds",
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
    "member_get",
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
    if (typeof fn.name !== "string" || fn.name.length === 0) err(`functions[${fi}].name must be non-empty string`);
    if (!fn.returnType || fn.returnType.kind !== "IRType" || typeof fn.returnType.name !== "string") {
      err(`functions[${fi}].returnType must be IRType`);
    }
    if (!Array.isArray(fn.params)) err(`functions[${fi}].params must be array`);
    if (!Array.isArray(fn.blocks) || fn.blocks.length === 0) err(`functions[${fi}].blocks must be non-empty array`);
    if (!Array.isArray(fn.blocks) || fn.blocks.length === 0) continue;

    const labels = new Set();
    for (let bi = 0; bi < fn.blocks.length; bi += 1) {
      const b = fn.blocks[bi];
      if (!b || typeof b !== "object") {
        err(`functions[${fi}].blocks[${bi}] must be object`);
        continue;
      }
      if (b.kind !== "Block") err(`functions[${fi}].blocks[${bi}].kind must be 'Block'`);
      if (typeof b.label !== "string" || b.label.length === 0) err(`functions[${fi}].blocks[${bi}].label must be non-empty string`);
      if (labels.has(b.label)) err(`functions[${fi}] duplicated block label '${b.label}'`);
      labels.add(b.label);
      if (!Array.isArray(b.instrs)) {
        err(`functions[${fi}].blocks[${bi}].instrs must be array`);
        continue;
      }
      for (let ii = 0; ii < b.instrs.length; ii += 1) {
        const ins = b.instrs[ii];
        if (!ins || typeof ins !== "object") {
          err(`functions[${fi}].blocks[${bi}].instrs[${ii}] must be object`);
          continue;
        }
        if (typeof ins.op !== "string" || ins.op.length === 0) {
          err(`functions[${fi}].blocks[${bi}].instrs[${ii}].op must be non-empty string`);
          continue;
        }
        if (!knownOps.has(ins.op)) err(`functions[${fi}].blocks[${bi}].instrs[${ii}].op unknown '${ins.op}'`);
      }
    }

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
