"use strict";

function deepClone(x) {
  return JSON.parse(JSON.stringify(x));
}

function isCheckOp(op) {
  return (
    op === "check_int_overflow" ||
    op === "check_int_overflow_unary_minus" ||
    op === "check_div_zero" ||
    op === "check_shift_range" ||
    op === "check_index_bounds" ||
    op === "check_map_has_key"
  );
}

function reduceRedundantChecks(ir) {
  for (const fn of ir.functions) {
    for (const b of fn.blocks) {
      const seen = new Set();
      const out = [];
      for (const i of b.instrs) {
        if (!isCheckOp(i.op)) {
          out.push(i);
          continue;
        }
        const key = `${i.op}|${JSON.stringify(i)}`;
        if (seen.has(key)) continue;
        seen.add(key);
        out.push(i);
      }
      b.instrs = out;
    }
  }
}

function foldUnary(op, v) {
  if (op === "-") return -v;
  if (op === "!") return !v;
  if (op === "~") return ~v;
  return null;
}

function foldBinary(op, a, b) {
  switch (op) {
    case "+":
      return a + b;
    case "-":
      return a - b;
    case "*":
      return a * b;
    case "/":
      return a / b;
    case "%":
      return a % b;
    case "<<":
      return a << b;
    case ">>":
      return a >> b;
    case "&":
      return a & b;
    case "|":
      return a | b;
    case "^":
      return a ^ b;
    case "==":
      return a === b;
    case "!=":
      return a !== b;
    case "<":
      return a < b;
    case "<=":
      return a <= b;
    case ">":
      return a > b;
    case ">=":
      return a >= b;
    case "&&":
      return Boolean(a && b);
    case "||":
      return Boolean(a || b);
    default:
      return null;
  }
}

function constantPropagation(ir) {
  for (const fn of ir.functions) {
    for (const b of fn.blocks) {
      const cst = new Map();
      const out = [];
      for (const i of b.instrs) {
        const clearDst = () => {
          if (i.dst) cst.delete(i.dst);
        };
        if (i.op === "const") {
          cst.set(i.dst, { literalType: i.literalType, value: i.value });
          out.push(i);
          continue;
        }
        if (i.op === "copy") {
          if (cst.has(i.src)) cst.set(i.dst, cst.get(i.src));
          else cst.delete(i.dst);
          out.push(i);
          continue;
        }
        if (i.op === "unary_op" && cst.has(i.src)) {
          const src = cst.get(i.src);
          const n = Number(src.value);
          if (Number.isFinite(n) || typeof src.value === "boolean") {
            const folded = foldUnary(i.operator, Number.isFinite(n) ? n : src.value);
            if (folded !== null) {
              const litType = typeof folded === "boolean" ? "bool" : src.literalType;
              const ni = { op: "const", dst: i.dst, literalType: litType, value: String(folded), loc: i.loc };
              cst.set(i.dst, { literalType: litType, value: String(folded) });
              out.push(ni);
              continue;
            }
          }
        }
        if (i.op === "bin_op" && cst.has(i.left) && cst.has(i.right)) {
          const a = cst.get(i.left);
          const b2 = cst.get(i.right);
          const av = a.literalType === "bool" ? a.value === "true" : Number(a.value);
          const bv = b2.literalType === "bool" ? b2.value === "true" : Number(b2.value);
          const folded = foldBinary(i.operator, av, bv);
          if (folded !== null && folded !== undefined && Number.isFinite(folded) || typeof folded === "boolean") {
            const litType = typeof folded === "boolean" ? "bool" : a.literalType;
            const ni = { op: "const", dst: i.dst, literalType: litType, value: String(folded), loc: i.loc };
            cst.set(i.dst, { literalType: litType, value: String(folded) });
            out.push(ni);
            continue;
          }
        }
        if (i.op === "select" && cst.has(i.cond)) {
          const cond = cst.get(i.cond).value === "true";
          const src = cond ? i.thenValue : i.elseValue;
          const ni = { op: "copy", dst: i.dst, src, loc: i.loc };
          if (cst.has(src)) cst.set(i.dst, cst.get(src));
          else cst.delete(i.dst);
          out.push(ni);
          continue;
        }
        clearDst();
        out.push(i);
      }
      b.instrs = out;
    }
  }
}

function collectInlineCandidates(ir) {
  const cands = new Map();
  for (const fn of ir.functions) {
    if (fn.blocks.length !== 1) continue;
    const b = fn.blocks[0];
    if (b.instrs.length === 0) continue;
    const last = b.instrs[b.instrs.length - 1];
    if (last.op !== "ret") continue;
    let ok = true;
    for (let i = 0; i < b.instrs.length - 1; i += 1) {
      const op = b.instrs[i].op;
      if (
        ![
          "const",
          "load_var",
          "unary_op",
          "bin_op",
          "select",
          "copy",
          "check_int_overflow",
          "check_int_overflow_unary_minus",
          "check_div_zero",
          "check_shift_range",
        ].includes(op)
      ) {
        ok = false;
        break;
      }
    }
    if (ok) cands.set(fn.name, fn);
  }
  return cands;
}

function inlineCalls(ir) {
  const cands = collectInlineCandidates(ir);
  let tid = 1000000;
  const nextTemp = () => `%opt${++tid}`;

  for (const fn of ir.functions) {
    for (const b of fn.blocks) {
      const out = [];
      for (const i of b.instrs) {
        if (i.op !== "call_static" || !cands.has(i.callee)) {
          out.push(i);
          continue;
        }
        const callee = cands.get(i.callee);
        if (callee.params.length !== i.args.length) {
          out.push(i);
          continue;
        }
        const paramMap = new Map();
        callee.params.forEach((p, idx) => paramMap.set(p.name, i.args[idx]));
        const tempMap = new Map();

        for (const ci of callee.blocks[0].instrs) {
          if (ci.op === "ret") {
            const src = tempMap.get(ci.value) || paramMap.get(ci.value) || ci.value;
            out.push({ op: "copy", dst: i.dst, src, loc: i.loc });
            continue;
          }
          if (ci.op === "const") {
            const dst = nextTemp();
            tempMap.set(ci.dst, dst);
            out.push({ ...ci, dst });
            continue;
          }
          if (ci.op === "load_var") {
            const dst = nextTemp();
            tempMap.set(ci.dst, dst);
            const src = paramMap.get(ci.name) || ci.name;
            out.push({ op: "copy", dst, src, loc: ci.loc || i.loc });
            continue;
          }
          if (ci.op === "copy") {
            const dst = nextTemp();
            tempMap.set(ci.dst, dst);
            const src = tempMap.get(ci.src) || paramMap.get(ci.src) || ci.src;
            out.push({ op: "copy", dst, src, loc: ci.loc || i.loc });
            continue;
          }
          if (ci.op === "unary_op") {
            const dst = nextTemp();
            tempMap.set(ci.dst, dst);
            const src = tempMap.get(ci.src) || paramMap.get(ci.src) || ci.src;
            out.push({ ...ci, dst, src });
            continue;
          }
          if (ci.op === "check_int_overflow") {
            const left = tempMap.get(ci.left) || paramMap.get(ci.left) || ci.left;
            const right = tempMap.get(ci.right) || paramMap.get(ci.right) || ci.right;
            out.push({ ...ci, left, right });
            continue;
          }
          if (ci.op === "check_int_overflow_unary_minus") {
            const value = tempMap.get(ci.value) || paramMap.get(ci.value) || ci.value;
            out.push({ ...ci, value });
            continue;
          }
          if (ci.op === "check_div_zero") {
            const divisor = tempMap.get(ci.divisor) || paramMap.get(ci.divisor) || ci.divisor;
            out.push({ ...ci, divisor });
            continue;
          }
          if (ci.op === "check_shift_range") {
            const shift = tempMap.get(ci.shift) || paramMap.get(ci.shift) || ci.shift;
            out.push({ ...ci, shift });
            continue;
          }
          if (ci.op === "bin_op") {
            const dst = nextTemp();
            tempMap.set(ci.dst, dst);
            const left = tempMap.get(ci.left) || paramMap.get(ci.left) || ci.left;
            const right = tempMap.get(ci.right) || paramMap.get(ci.right) || ci.right;
            out.push({ ...ci, dst, left, right });
            continue;
          }
          if (ci.op === "select") {
            const dst = nextTemp();
            tempMap.set(ci.dst, dst);
            const cond = tempMap.get(ci.cond) || paramMap.get(ci.cond) || ci.cond;
            const thenValue = tempMap.get(ci.thenValue) || paramMap.get(ci.thenValue) || ci.thenValue;
            const elseValue = tempMap.get(ci.elseValue) || paramMap.get(ci.elseValue) || ci.elseValue;
            out.push({ ...ci, dst, cond, thenValue, elseValue });
            continue;
          }
          out.push(i);
          break;
        }
      }
      b.instrs = out;
    }
  }
}

function optimizeIR(inputIR) {
  const ir = deepClone(inputIR);
  inlineCalls(ir);
  constantPropagation(ir);
  reduceRedundantChecks(ir);
  return ir;
}

module.exports = {
  optimizeIR,
};
