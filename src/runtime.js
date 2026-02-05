"use strict";

const INT64_MIN = -(2n ** 63n);
const INT64_MAX = 2n ** 63n - 1n;

class RuntimeError extends Error {
  constructor(diag) {
    super(diag.message);
    this.diag = diag;
  }
}

class ReturnSignal {
  constructor(value) {
    this.value = value;
  }
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

function runProgram(ast, file) {
  const functions = new Map();
  for (const d of ast.decls) {
    if (d.kind === "FunctionDecl") functions.set(d.name, d);
  }
  const main = functions.get("main");
  if (!main) return;

  const callFunction = (fn, args) => {
    const scope = new Scope(null);
    for (let i = 0; i < fn.params.length; i += 1) {
      scope.define(fn.params[i].name, args[i]);
    }
    try {
      execBlock(fn.body, scope, functions, file, callFunction);
      return null;
    } catch (e) {
      if (e instanceof ReturnSignal) return e.value;
      throw e;
    }
  };

  callFunction(main, []);
}

function execBlock(block, scope, functions, file, callFunction) {
  const local = new Scope(scope);
  for (const stmt of block.stmts) execStmt(stmt, local, functions, file, callFunction);
}

function execStmt(stmt, scope, functions, file, callFunction) {
  switch (stmt.kind) {
    case "VarDecl": {
      let v = null;
      if (stmt.init) v = evalExpr(stmt.init, scope, functions, file, callFunction);
      scope.define(stmt.name, v);
      return;
    }
    case "AssignStmt": {
      const rhs = evalExpr(stmt.expr, scope, functions, file, callFunction);
      if (stmt.target.kind === "Identifier") {
        scope.set(stmt.target.name, rhs);
      } else if (stmt.target.kind === "IndexExpr") {
        const target = evalExpr(stmt.target.target, scope, functions, file, callFunction);
        const idx = evalExpr(stmt.target.index, scope, functions, file, callFunction);
        assignIndex(file, stmt.target.target, stmt.target.index, target, idx, rhs);
      }
      return;
    }
    case "ExprStmt":
      evalExpr(stmt.expr, scope, functions, file, callFunction);
      return;
    case "ReturnStmt": {
      const v = stmt.expr ? evalExpr(stmt.expr, scope, functions, file, callFunction) : null;
      throw new ReturnSignal(v);
    }
    case "ThrowStmt":
      throw new RuntimeError(rdiag(file, stmt, "R1999", "RUNTIME_THROW", "explicit throw"));
    case "Block":
      execBlock(stmt, scope, functions, file, callFunction);
      return;
    case "ForStmt":
      execFor(stmt, scope, functions, file, callFunction);
      return;
    case "SwitchStmt":
      execSwitch(stmt, scope, functions, file, callFunction);
      return;
    case "BreakStmt":
    case "ContinueStmt":
      return;
    default:
      return;
  }
}

function execFor(stmt, scope, functions, file, callFunction) {
  if (stmt.forKind === "classic") {
    const s = new Scope(scope);
    if (stmt.init) {
      if (stmt.init.kind === "VarDecl" || stmt.init.kind === "AssignStmt") execStmt(stmt.init, s, functions, file, callFunction);
      else evalExpr(stmt.init, s, functions, file, callFunction);
    }
    while (true) {
      if (stmt.cond) {
        const c = evalExpr(stmt.cond, s, functions, file, callFunction);
        if (!Boolean(c)) break;
      }
      execStmt(stmt.body, s, functions, file, callFunction);
      if (stmt.step) {
        if (stmt.step.kind === "VarDecl" || stmt.step.kind === "AssignStmt") execStmt(stmt.step, s, functions, file, callFunction);
        else evalExpr(stmt.step, s, functions, file, callFunction);
      }
    }
    return;
  }

  const seq = evalExpr(stmt.iterExpr, scope, functions, file, callFunction);
  const s = new Scope(scope);
  let items = null;
  if (Array.isArray(seq)) {
    items = seq;
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
    execStmt(stmt.body, s, functions, file, callFunction);
  }
}

function execSwitch(stmt, scope, functions, file, callFunction) {
  const v = evalExpr(stmt.expr, scope, functions, file, callFunction);
  for (const c of stmt.cases) {
    const cv = evalExpr(c.value, scope, functions, file, callFunction);
    if (eqValue(v, cv)) {
      for (const st of c.stmts) execStmt(st, scope, functions, file, callFunction);
      return;
    }
  }
  if (stmt.defaultCase) {
    for (const st of stmt.defaultCase.stmts) execStmt(st, scope, functions, file, callFunction);
  }
}

function eqValue(a, b) {
  if (typeof a === "bigint" || typeof b === "bigint") return BigInt(a) === BigInt(b);
  return a === b;
}

function evalExpr(expr, scope, functions, file, callFunction) {
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
      const v = evalExpr(expr.expr, scope, functions, file, callFunction);
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
      const l = evalExpr(expr.left, scope, functions, file, callFunction);
      const r = evalExpr(expr.right, scope, functions, file, callFunction);
      return evalBinary(file, expr, l, r);
    }
    case "ConditionalExpr": {
      const c = evalExpr(expr.cond, scope, functions, file, callFunction);
      return c ? evalExpr(expr.thenExpr, scope, functions, file, callFunction) : evalExpr(expr.elseExpr, scope, functions, file, callFunction);
    }
    case "CallExpr":
      return evalCall(expr, scope, functions, file, callFunction);
    case "IndexExpr": {
      const target = evalExpr(expr.target, scope, functions, file, callFunction);
      const idx = evalExpr(expr.index, scope, functions, file, callFunction);
      return indexGet(file, expr.target, expr.index, target, idx);
    }
    case "MemberExpr": {
      const target = evalExpr(expr.target, scope, functions, file, callFunction);
      return { __member__: true, target, name: expr.name, node: expr };
    }
    case "PostfixExpr": {
      const v = evalExpr(expr.expr, scope, functions, file, callFunction);
      return v;
    }
    case "ListLiteral":
      return expr.items.map((it) => evalExpr(it, scope, functions, file, callFunction));
    case "MapLiteral": {
      const m = new Map();
      for (const p of expr.pairs) {
        const k = evalExpr(p.key, scope, functions, file, callFunction);
        const v = evalExpr(p.value, scope, functions, file, callFunction);
        m.set(mapKey(k), v);
      }
      return m;
    }
    default:
      return null;
  }
}

function mapKey(v) {
  if (typeof v === "bigint") return `i:${v.toString()}`;
  return `${typeof v}:${String(v)}`;
}

function unmapKey(k) {
  if (k.startsWith("i:")) return BigInt(k.slice(2));
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
  if (typeof target === "string") {
    const i = Number(idx);
    if (!Number.isInteger(i) || i < 0 || i >= target.length) {
      throw new RuntimeError(rdiag(file, targetNode, "R1002", "RUNTIME_INDEX_OOB", "index out of bounds"));
    }
    return target[i];
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
  if (target instanceof Map) {
    target.set(mapKey(idx), rhs);
  }
}

function evalCall(expr, scope, functions, file, callFunction) {
  // Function call by identifier.
  if (expr.callee.kind === "Identifier") {
    const fn = functions.get(expr.callee.name);
    if (!fn) return null;
    const args = expr.args.map((a) => evalExpr(a, scope, functions, file, callFunction));
    return callFunction(fn, args);
  }

  // Member call.
  if (expr.callee.kind === "MemberExpr") {
    const m = expr.callee;
    const target = evalExpr(m.target, scope, functions, file, callFunction);
    const args = expr.args.map((a) => evalExpr(a, scope, functions, file, callFunction));

    // Sys.print(...)
    if (m.target.kind === "Identifier" && m.target.name === "Sys" && m.name === "print") {
      const s = args.map((a) => (typeof a === "bigint" ? a.toString() : String(a))).join("");
      // Keep behavior explicit and deterministic.
      process.stdout.write(`${s}\n`);
      return null;
    }

    if (m.name === "toString") {
      if (target === null || target === undefined) return "null";
      if (typeof target === "bigint") return target.toString();
      if (typeof target === "string") return target;
      if (typeof target === "boolean") return target ? "true" : "false";
      return String(target);
    }

    if (m.name === "length") {
      if (Array.isArray(target) || typeof target === "string") return BigInt(target.length);
      if (target instanceof Map) return BigInt(target.size);
      return 0n;
    }
  }

  return null;
}

module.exports = {
  runProgram,
  RuntimeError,
};
