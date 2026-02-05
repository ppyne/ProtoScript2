"use strict";

function baseName(typeName) {
  return typeName.replace(/[^A-Za-z0-9_]/g, "_");
}

function cScalarType(typeName) {
  switch (typeName) {
    case "int":
      return "int64_t";
    case "float":
      return "double";
    case "bool":
      return "bool";
    case "byte":
      return "uint8_t";
    case "glyph":
      return "uint32_t";
    case "string":
      return "ps_string";
    case "void":
      return "void";
    case "iter_cursor":
      return "ps_iter_cursor";
    default:
      return null;
  }
}

function parseContainer(typeName) {
  const m = /^([a-zA-Z_][a-zA-Z0-9_]*)<(.*)>$/.exec(typeName);
  if (!m) return null;
  return { kind: m[1], inner: m[2] };
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

function parseMapType(typeName) {
  const c = parseContainer(typeName);
  if (!c || c.kind !== "map") return null;
  const args = splitTypeArgs(c.inner);
  if (args.length !== 2) return null;
  return {
    keyType: args[0],
    valueType: args[1],
    base: baseName(c.inner),
  };
}

function isAliasTrackedType(typeName) {
  if (parseMapType(typeName)) return true;
  const c = parseContainer(typeName);
  return !!(c && c.kind === "list");
}

function cTypeFromName(typeName) {
  const scalar = cScalarType(typeName);
  if (scalar) return scalar;
  const c = parseContainer(typeName);
  if (!c) return "int64_t";
  const inner = baseName(c.inner);
  if (c.kind === "list") return `ps_list_${inner}`;
  if (c.kind === "view") return `ps_view_${inner}`;
  if (c.kind === "slice") return `ps_slice_${inner}`;
  if (c.kind === "map") return `ps_map_${inner}`;
  return "int64_t";
}

function classifyBinOp(op) {
  if (["==", "!=", "<", "<=", ">", ">="].includes(op)) return "cmp";
  if (["&&", "||"].includes(op)) return "logic";
  return "arith";
}

function inferTempTypes(ir) {
  const fnRet = new Map(ir.functions.map((f) => [f.name, f.returnType.name]));
  const result = new Map();

  for (const fn of ir.functions) {
    const varTypes = new Map();
    for (const p of fn.params) varTypes.set(p.name, p.type.name);

    const tempTypes = new Map();
    let changed = true;
    while (changed) {
      changed = false;
      for (const b of fn.blocks) {
        for (const i of b.instrs) {
          const set = (k, v) => {
            if (!k || !v) return;
            if (!tempTypes.has(k)) {
              tempTypes.set(k, v);
              changed = true;
            }
          };
          const getType = (k) => tempTypes.get(k) || varTypes.get(k) || null;
          switch (i.op) {
            case "var_decl":
              varTypes.set(i.name, i.type.name);
              break;
            case "const":
              set(i.dst, i.literalType);
              break;
            case "copy":
              if (tempTypes.has(i.src)) set(i.dst, tempTypes.get(i.src));
              break;
            case "load_var":
              set(i.dst, i.type.name);
              break;
            case "store_var":
              if (tempTypes.has(i.src) && !varTypes.has(i.name)) varTypes.set(i.name, tempTypes.get(i.src));
              if (varTypes.has(i.name) && !tempTypes.has(i.src)) set(i.src, varTypes.get(i.name));
              break;
            case "unary_op":
            case "postfix_op":
              if (tempTypes.has(i.src)) set(i.dst, tempTypes.get(i.src));
              break;
            case "bin_op": {
              const cls = classifyBinOp(i.operator);
              if (cls === "cmp" || cls === "logic") set(i.dst, "bool");
              else if (tempTypes.has(i.left)) set(i.dst, tempTypes.get(i.left));
              break;
            }
            case "call_static":
              if (fnRet.has(i.callee)) set(i.dst, fnRet.get(i.callee));
              break;
            case "call_method_static":
              if (getType(i.receiver)) {
                const rt = getType(i.receiver);
                const rc = parseContainer(rt);
                if (rt === "int") {
                  if (i.method === "toByte") set(i.dst, "byte");
                  else if (i.method === "toFloat") set(i.dst, "float");
                  else if (i.method === "toString") set(i.dst, "string");
                  else set(i.dst, "int");
                } else if (rt === "byte") {
                  if (i.method === "toInt") set(i.dst, "int");
                  else if (i.method === "toFloat") set(i.dst, "float");
                  else if (i.method === "toString") set(i.dst, "string");
                  else set(i.dst, "byte");
                } else if (rt === "float") {
                  if (i.method === "toInt") set(i.dst, "int");
                  else if (i.method === "toString") set(i.dst, "string");
                  else if (i.method === "abs") set(i.dst, "float");
                  else if (["isNaN", "isInfinite", "isFinite"].includes(i.method)) set(i.dst, "bool");
                  else set(i.dst, "float");
                } else if (rt === "string") {
                  if (["length", "indexOf"].includes(i.method)) set(i.dst, "int");
                  else if (["startsWith", "endsWith"].includes(i.method)) set(i.dst, "bool");
                  else if (i.method === "split") set(i.dst, "list<string>");
                  else if (i.method === "toUtf8Bytes") set(i.dst, "list<byte>");
                  else if (["substring", "trim", "trimStart", "trimEnd", "replace", "toUpper", "toLower", "concat"].includes(i.method)) set(i.dst, "string");
                } else if (rc && rc.kind === "list" && rc.inner === "byte" && i.method === "toUtf8String") {
                  set(i.dst, "string");
                } else if (i.method === "length") {
                  set(i.dst, "int");
                } else {
                  set(i.dst, "int");
                }
              } else {
                set(i.dst, "int");
              }
              break;
            case "call_unknown":
              set(i.dst, "int");
              break;
            case "call_builtin_tostring":
              set(i.dst, "string");
              break;
            case "select":
              if (tempTypes.has(i.thenValue)) set(i.dst, tempTypes.get(i.thenValue));
              break;
            case "make_view":
              if (getType(i.source)) {
                const src = getType(i.source);
                const p = parseContainer(src);
                if (src === "string") {
                  if (i.kind === "view") set(i.dst, "view<glyph>");
                  if (i.kind === "slice") set(i.dst, "slice<glyph>");
                } else if (p && ["list", "slice", "view"].includes(p.kind)) {
                  if (i.kind === "view") set(i.dst, `view<${p.inner}>`);
                  if (i.kind === "slice") set(i.dst, `slice<${p.inner}>`);
                }
              }
              break;
            case "iter_begin":
              set(i.dst, "iter_cursor");
              break;
            case "iter_next":
              if (getType(i.source)) {
                const src = getType(i.source);
                if (src === "string") set(i.dst, "glyph");
                const p = parseContainer(src);
                if (p && ["list", "slice", "view"].includes(p.kind)) set(i.dst, p.inner);
                if (p && p.kind === "map") {
                  const args = splitTypeArgs(p.inner);
                  if (args.length === 2) set(i.dst, i.mode === "in" ? args[0] : args[1]);
                }
              }
              break;
            case "make_list":
              if (i.items.length > 0 && tempTypes.has(i.items[0])) set(i.dst, `list<${tempTypes.get(i.items[0])}>`);
              break;
            case "make_map":
              if (i.pairs.length > 0) {
                const first = i.pairs[0];
                if (getType(first.key) && getType(first.value)) set(i.dst, `map<${getType(first.key)},${getType(first.value)}>`);
              }
              break;
            case "index_get":
              if (getType(i.target)) {
                const t = getType(i.target);
                const p = parseContainer(t);
                if (p && ["list", "slice", "view"].includes(p.kind)) set(i.dst, p.inner);
                if (p && p.kind === "map") {
                  const args = splitTypeArgs(p.inner);
                  if (args.length === 2) set(i.dst, args[1]);
                }
                if (t === "string") set(i.dst, "glyph");
              }
              break;
            default:
              break;
          }
        }
      }
    }
    result.set(fn.name, { varTypes, tempTypes });
  }
  return result;
}

function collectTypeNames(ir, inferred) {
  const names = new Set(["int", "float", "bool", "byte", "glyph", "string", "void", "iter_cursor", "list<string>", "list<byte>"]);
  for (const fn of ir.functions) {
    names.add(fn.returnType.name);
    for (const p of fn.params) names.add(p.type.name);
    const inf = inferred.get(fn.name);
    for (const t of inf.varTypes.values()) names.add(t);
    for (const t of inf.tempTypes.values()) names.add(t);
  }
  return names;
}

function emitTypeDecls(typeNames) {
  const out = [];
  out.push("typedef struct { const char* ptr; size_t len; } ps_string;");
  out.push("typedef struct { size_t i; size_t n; } ps_iter_cursor;");
  for (const t of typeNames) {
    const p = parseContainer(t);
    if (!p) continue;
    const innerC = cTypeFromName(p.inner);
    const bn = baseName(p.inner);
    if (p.kind === "list") {
      out.push(`typedef struct { ${innerC}* ptr; size_t len; size_t cap; } ps_list_${bn};`);
    } else if (p.kind === "view") {
      out.push(`typedef struct { ${innerC}* ptr; size_t len; } ps_view_${bn};`);
    } else if (p.kind === "slice") {
      out.push(`typedef struct { ${innerC}* ptr; size_t len; } ps_slice_${bn};`);
    } else if (p.kind === "map") {
      const m = parseMapType(t);
      if (m) {
        out.push(
          `typedef struct { ${cTypeFromName(m.keyType)}* keys; ${cTypeFromName(m.valueType)}* values; size_t len; size_t cap; } ps_map_${bn};`
        );
      }
    }
  }
  return out;
}

function keyEqExpr(a, b, keyType) {
  if (keyType === "string") return `(${a}.len == ${b}.len && memcmp(${a}.ptr, ${b}.ptr, ${a}.len) == 0)`;
  return `(${a} == ${b})`;
}

function emitContainerHelpers(typeNames) {
  const out = [];
  const seenMaps = new Set();
  for (const t of typeNames) {
    const m = parseMapType(t);
    if (!m || seenMaps.has(m.base)) continue;
    seenMaps.add(m.base);
    const mapC = `ps_map_${m.base}`;
    const keyC = cTypeFromName(m.keyType);
    const valC = cTypeFromName(m.valueType);
    const eq = keyEqExpr("m->keys[i]", "key", m.keyType);
    out.push(`static bool ps_map_find_${m.base}(const ${mapC}* m, ${keyC} key, size_t* out_idx) {`);
    out.push("  for (size_t i = 0; i < m->len; i += 1) {");
    out.push(`    if ${eq} {`);
    out.push("      if (out_idx) *out_idx = i;");
    out.push("      return true;");
    out.push("    }");
    out.push("  }");
    out.push("  return false;");
    out.push("}");
    out.push(`static bool ps_map_has_key_${m.base}(const ${mapC}* m, ${keyC} key) {`);
    out.push(`  return ps_map_find_${m.base}(m, key, NULL);`);
    out.push("}");
    out.push(`static ${valC} ps_map_get_${m.base}(const ${mapC}* m, ${keyC} key) {`);
    out.push("  size_t idx = 0;");
    out.push(`  bool ok = ps_map_find_${m.base}(m, key, &idx);`);
    out.push('  if (!ok) ps_panic("R1003", "RUNTIME_MISSING_KEY", "missing map key");');
    out.push("  return m->values[idx];");
    out.push("}");
    out.push(`static void ps_map_set_${m.base}(${mapC}* m, ${keyC} key, ${valC} value) {`);
    out.push("  size_t idx = 0;");
    out.push(`  if (ps_map_find_${m.base}(m, key, &idx)) {`);
    out.push("    m->values[idx] = value;");
    out.push("    return;");
    out.push("  }");
    out.push("  if (m->len == m->cap) {");
    out.push("    size_t new_cap = (m->cap == 0) ? 4 : (m->cap * 2);");
    out.push("    m->keys = realloc(m->keys, sizeof(*m->keys) * new_cap);");
    out.push("    m->values = realloc(m->values, sizeof(*m->values) * new_cap);");
    out.push("    if (!m->keys || !m->values) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");");
    out.push("    m->cap = new_cap;");
    out.push("  }");
    out.push("  m->keys[m->len] = key;");
    out.push("  m->values[m->len] = value;");
    out.push("  m->len += 1;");
    out.push("}");
  }
  return out;
}

function emitRuntimeHelpers() {
  return [
    "static void ps_panic(const char* code, const char* category, const char* msg) {",
    '  fprintf(stderr, "<runtime>:1:1 %s %s: %s\\n", code, category, msg);',
    "  exit(1);",
    "}",
    "",
    "static void ps_check_int_overflow_add(int64_t a, int64_t b) {",
    "  int64_t r;",
    "  if (__builtin_add_overflow(a, b, &r)) ps_panic(\"R1001\", \"RUNTIME_INT_OVERFLOW\", \"int overflow on +\");",
    "}",
    "static void ps_check_int_overflow_sub(int64_t a, int64_t b) {",
    "  int64_t r;",
    "  if (__builtin_sub_overflow(a, b, &r)) ps_panic(\"R1001\", \"RUNTIME_INT_OVERFLOW\", \"int overflow on -\");",
    "}",
    "static void ps_check_int_overflow_mul(int64_t a, int64_t b) {",
    "  int64_t r;",
    "  if (__builtin_mul_overflow(a, b, &r)) ps_panic(\"R1001\", \"RUNTIME_INT_OVERFLOW\", \"int overflow on *\");",
    "}",
    "static void ps_check_div_zero_int(int64_t d) {",
    "  if (d == 0) ps_panic(\"R1004\", \"RUNTIME_DIVIDE_BY_ZERO\", \"division by zero\");",
    "}",
    "static void ps_check_shift_range(int64_t s, int width) {",
    "  if (s < 0 || s >= width) ps_panic(\"R1005\", \"RUNTIME_SHIFT_RANGE\", \"invalid shift amount\");",
    "}",
    "static void ps_check_index_bounds(size_t len, int64_t idx) {",
    "  if (idx < 0 || (size_t)idx >= len) ps_panic(\"R1002\", \"RUNTIME_INDEX_OOB\", \"index out of bounds\");",
    "}",
    "static void ps_check_map_has_key(int has_key) {",
    "  if (!has_key) ps_panic(\"R1003\", \"RUNTIME_MISSING_KEY\", \"missing map key\");",
    "}",
    "static size_t ps_utf8_next(const char* s, size_t len, size_t i, uint32_t* out_cp) {",
    "  if (i >= len) return 0;",
    "  unsigned char c0 = (unsigned char)s[i];",
    "  if (c0 <= 0x7F) { if (out_cp) *out_cp = c0; return 1; }",
    "  if (c0 >= 0xC2 && c0 <= 0xDF) {",
    "    if (i + 1 >= len) return 0;",
    "    unsigned char c1 = (unsigned char)s[i + 1];",
    "    if ((c1 & 0xC0) != 0x80) return 0;",
    "    if (out_cp) *out_cp = ((uint32_t)(c0 & 0x1F) << 6) | (uint32_t)(c1 & 0x3F);",
    "    return 2;",
    "  }",
    "  if (c0 >= 0xE0 && c0 <= 0xEF) {",
    "    if (i + 2 >= len) return 0;",
    "    unsigned char c1 = (unsigned char)s[i + 1];",
    "    unsigned char c2 = (unsigned char)s[i + 2];",
    "    if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return 0;",
    "    if (c0 == 0xE0 && c1 < 0xA0) return 0;",
    "    if (c0 == 0xED && c1 > 0x9F) return 0;",
    "    uint32_t cp = ((uint32_t)(c0 & 0x0F) << 12) | ((uint32_t)(c1 & 0x3F) << 6) | (uint32_t)(c2 & 0x3F);",
    "    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;",
    "    if (out_cp) *out_cp = cp;",
    "    return 3;",
    "  }",
    "  if (c0 >= 0xF0 && c0 <= 0xF4) {",
    "    if (i + 3 >= len) return 0;",
    "    unsigned char c1 = (unsigned char)s[i + 1];",
    "    unsigned char c2 = (unsigned char)s[i + 2];",
    "    unsigned char c3 = (unsigned char)s[i + 3];",
    "    if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return 0;",
    "    if (c0 == 0xF0 && c1 < 0x90) return 0;",
    "    if (c0 == 0xF4 && c1 > 0x8F) return 0;",
    "    uint32_t cp = ((uint32_t)(c0 & 0x07) << 18) | ((uint32_t)(c1 & 0x3F) << 12) | ((uint32_t)(c2 & 0x3F) << 6) | (uint32_t)(c3 & 0x3F);",
    "    if (cp > 0x10FFFF) return 0;",
    "    if (out_cp) *out_cp = cp;",
    "    return 4;",
    "  }",
    "  return 0;",
    "}",
    "static bool ps_utf8_validate(const uint8_t* s, size_t len) {",
    "  size_t i = 0;",
    "  while (i < len) {",
    "    size_t adv = ps_utf8_next((const char*)s, len, i, NULL);",
    "    if (adv == 0) return false;",
    "    i += adv;",
    "  }",
    "  return true;",
    "}",
    "static size_t ps_utf8_glyph_len(ps_string s) {",
    "  size_t n = 0;",
    "  for (size_t i = 0; i < s.len; ) {",
    "    size_t adv = ps_utf8_next(s.ptr, s.len, i, NULL);",
    "    if (adv == 0) ps_panic(\"R1007\", \"RUNTIME_INVALID_UTF8\", \"invalid UTF-8\");",
    "    i += adv;",
    "    n += 1;",
    "  }",
    "  return n;",
    "}",
    "static uint32_t ps_string_index_glyph(ps_string s, int64_t idx) {",
    "  if (idx < 0) ps_panic(\"R1002\", \"RUNTIME_INDEX_OOB\", \"index out of bounds\");",
    "  size_t want = (size_t)idx;",
    "  size_t g = 0;",
    "  for (size_t i = 0; i < s.len; ) {",
    "    uint32_t cp = 0;",
    "    size_t adv = ps_utf8_next(s.ptr, s.len, i, &cp);",
    "    if (adv == 0) ps_panic(\"R1007\", \"RUNTIME_INVALID_UTF8\", \"invalid UTF-8\");",
    "    if (g == want) return cp;",
    "    g += 1;",
    "    i += adv;",
    "  }",
    "  ps_panic(\"R1002\", \"RUNTIME_INDEX_OOB\", \"index out of bounds\");",
    "  return 0;",
    "}",
    "static ps_string ps_string_substring(ps_string s, int64_t start, int64_t length) {",
    "  if (start < 0 || length < 0) ps_panic(\"R1002\", \"RUNTIME_INDEX_OOB\", \"index out of bounds\");",
    "  size_t want = (size_t)start;",
    "  size_t need = (size_t)length;",
    "  size_t g = 0;",
    "  size_t i = 0;",
    "  size_t start_b = 0;",
    "  size_t end_b = 0;",
    "  while (i < s.len && g < want) {",
    "    size_t adv = ps_utf8_next(s.ptr, s.len, i, NULL);",
    "    if (adv == 0) ps_panic(\"R1007\", \"RUNTIME_INVALID_UTF8\", \"invalid UTF-8\");",
    "    i += adv;",
    "    g += 1;",
    "  }",
    "  if (g != want) ps_panic(\"R1002\", \"RUNTIME_INDEX_OOB\", \"index out of bounds\");",
    "  start_b = i;",
    "  while (i < s.len && g < want + need) {",
    "    size_t adv = ps_utf8_next(s.ptr, s.len, i, NULL);",
    "    if (adv == 0) ps_panic(\"R1007\", \"RUNTIME_INVALID_UTF8\", \"invalid UTF-8\");",
    "    i += adv;",
    "    g += 1;",
    "  }",
    "  if (g != want + need) ps_panic(\"R1002\", \"RUNTIME_INDEX_OOB\", \"index out of bounds\");",
    "  end_b = i;",
    "  size_t out_len = end_b - start_b;",
    "  char* buf = (char*)malloc(out_len + 1);",
    "  if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  if (out_len > 0) memcpy(buf, s.ptr + start_b, out_len);",
    "  buf[out_len] = 0;",
    "  ps_string out = { buf, out_len };",
    "  return out;",
    "}",
    "static int64_t ps_string_index_of(ps_string s, ps_string needle) {",
    "  if (needle.len == 0) return 0;",
    "  size_t i = 0;",
    "  int64_t g = 0;",
    "  while (i + needle.len <= s.len) {",
    "    if (memcmp(s.ptr + i, needle.ptr, needle.len) == 0) return g;",
    "    size_t adv = ps_utf8_next(s.ptr, s.len, i, NULL);",
    "    if (adv == 0) ps_panic(\"R1007\", \"RUNTIME_INVALID_UTF8\", \"invalid UTF-8\");",
    "    i += adv;",
    "    g += 1;",
    "  }",
    "  return -1;",
    "}",
    "static bool ps_string_starts_with(ps_string s, ps_string prefix) {",
    "  if (prefix.len > s.len) return false;",
    "  return memcmp(s.ptr, prefix.ptr, prefix.len) == 0;",
    "}",
    "static bool ps_string_ends_with(ps_string s, ps_string suffix) {",
    "  if (suffix.len > s.len) return false;",
    "  return memcmp(s.ptr + (s.len - suffix.len), suffix.ptr, suffix.len) == 0;",
    "}",
    "static ps_string ps_string_trim(ps_string s, int mode) {",
    "  size_t start = 0;",
    "  size_t end = s.len;",
    "  if (mode != 2) {",
    "    while (start < end) {",
    "      char c = s.ptr[start];",
    "      if (c == ' ' || c == '\\t' || c == '\\n' || c == '\\r') start += 1; else break;",
    "    }",
    "  }",
    "  if (mode != 1) {",
    "    while (end > start) {",
    "      char c = s.ptr[end - 1];",
    "      if (c == ' ' || c == '\\t' || c == '\\n' || c == '\\r') end -= 1; else break;",
    "    }",
    "  }",
    "  size_t out_len = end - start;",
    "  char* buf = (char*)malloc(out_len + 1);",
    "  if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  if (out_len > 0) memcpy(buf, s.ptr + start, out_len);",
    "  buf[out_len] = 0;",
    "  ps_string out = { buf, out_len };",
    "  return out;",
    "}",
    "static ps_string ps_string_replace(ps_string s, ps_string oldv, ps_string newv) {",
    "  if (oldv.len == 0) return s;",
    "  size_t i = 0;",
    "  while (i + oldv.len <= s.len) {",
    "    if (memcmp(s.ptr + i, oldv.ptr, oldv.len) == 0) {",
    "      size_t out_len = s.len - oldv.len + newv.len;",
    "      char* buf = (char*)malloc(out_len + 1);",
    "      if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "      memcpy(buf, s.ptr, i);",
    "      memcpy(buf + i, newv.ptr, newv.len);",
    "      memcpy(buf + i + newv.len, s.ptr + i + oldv.len, s.len - (i + oldv.len));",
    "      buf[out_len] = 0;",
    "      ps_string out = { buf, out_len };",
    "      return out;",
    "    }",
    "    size_t adv = ps_utf8_next(s.ptr, s.len, i, NULL);",
    "    if (adv == 0) ps_panic(\"R1007\", \"RUNTIME_INVALID_UTF8\", \"invalid UTF-8\");",
    "    i += adv;",
    "  }",
    "  return s;",
    "}",
    "static ps_string ps_string_concat(ps_string a, ps_string b) {",
    "  size_t out_len = a.len + b.len;",
    "  char* buf = (char*)malloc(out_len + 1);",
    "  if (!buf && out_len > 0) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  if (a.len > 0) memcpy(buf, a.ptr, a.len);",
    "  if (b.len > 0) memcpy(buf + a.len, b.ptr, b.len);",
    "  buf[out_len] = 0;",
    "  ps_string out = { buf, out_len };",
    "  return out;",
    "}",
    "static ps_string ps_string_to_upper(ps_string s) {",
    "  char* buf = (char*)malloc(s.len + 1);",
    "  if (!buf && s.len > 0) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  for (size_t i = 0; i < s.len; i += 1) {",
    "    unsigned char c = (unsigned char)s.ptr[i];",
    "    if (c >= 'a' && c <= 'z') buf[i] = (char)(c - 32);",
    "    else buf[i] = (char)c;",
    "  }",
    "  buf[s.len] = 0;",
    "  ps_string out = { buf, s.len };",
    "  return out;",
    "}",
    "static ps_string ps_string_to_lower(ps_string s) {",
    "  char* buf = (char*)malloc(s.len + 1);",
    "  if (!buf && s.len > 0) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  for (size_t i = 0; i < s.len; i += 1) {",
    "    unsigned char c = (unsigned char)s.ptr[i];",
    "    if (c >= 'A' && c <= 'Z') buf[i] = (char)(c + 32);",
    "    else buf[i] = (char)c;",
    "  }",
    "  buf[s.len] = 0;",
    "  ps_string out = { buf, s.len };",
    "  return out;",
    "}",
    "static void ps_list_string_push(ps_list_string* l, ps_string v) {",
    "  if (l->len == l->cap) {",
    "    size_t new_cap = (l->cap == 0) ? 4 : (l->cap * 2);",
    "    l->ptr = (ps_string*)realloc(l->ptr, sizeof(*l->ptr) * new_cap);",
    "    if (!l->ptr) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    l->cap = new_cap;",
    "  }",
    "  l->ptr[l->len++] = v;",
    "}",
    "static ps_list_string ps_string_split(ps_string s, ps_string sep) {",
    "  ps_list_string out = { NULL, 0, 0 };",
    "  if (sep.len == 0) {",
    "    size_t i = 0;",
    "    while (i < s.len) {",
    "      size_t adv = ps_utf8_next(s.ptr, s.len, i, NULL);",
    "      if (adv == 0) ps_panic(\"R1007\", \"RUNTIME_INVALID_UTF8\", \"invalid UTF-8\");",
    "      char* buf = (char*)malloc(adv + 1);",
    "      if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "      memcpy(buf, s.ptr + i, adv);",
    "      buf[adv] = 0;",
    "      ps_string part = { buf, adv };",
    "      ps_list_string_push(&out, part);",
    "      i += adv;",
    "    }",
    "    return out;",
    "  }",
    "  size_t i = 0;",
    "  size_t last = 0;",
    "  while (i + sep.len <= s.len) {",
    "    if (memcmp(s.ptr + i, sep.ptr, sep.len) == 0) {",
    "      size_t out_len = i - last;",
    "      char* buf = (char*)malloc(out_len + 1);",
    "      if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "      if (out_len > 0) memcpy(buf, s.ptr + last, out_len);",
    "      buf[out_len] = 0;",
    "      ps_string part = { buf, out_len };",
    "      ps_list_string_push(&out, part);",
    "      i += sep.len;",
    "      last = i;",
    "      continue;",
    "    }",
    "    size_t adv = ps_utf8_next(s.ptr, s.len, i, NULL);",
    "    if (adv == 0) ps_panic(\"R1007\", \"RUNTIME_INVALID_UTF8\", \"invalid UTF-8\");",
    "    i += adv;",
    "  }",
    "  {",
    "    size_t out_len = s.len - last;",
    "    char* buf = (char*)malloc(out_len + 1);",
    "    if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    if (out_len > 0) memcpy(buf, s.ptr + last, out_len);",
    "    buf[out_len] = 0;",
    "    ps_string part = { buf, out_len };",
    "    ps_list_string_push(&out, part);",
    "  }",
    "  return out;",
    "}",
    "static ps_list_byte ps_string_to_utf8_bytes(ps_string s) {",
    "  ps_list_byte out = { NULL, 0, 0 };",
    "  out.len = s.len;",
    "  out.cap = s.len;",
    "  out.ptr = (uint8_t*)malloc(sizeof(uint8_t) * s.len);",
    "  if (!out.ptr && s.len > 0) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  if (s.len > 0) memcpy(out.ptr, s.ptr, s.len);",
    "  return out;",
    "}",
    "static ps_string ps_list_byte_to_utf8_string(ps_list_byte b) {",
    "  if (!ps_utf8_validate(b.ptr, b.len)) ps_panic(\"R1007\", \"RUNTIME_INVALID_UTF8\", \"invalid UTF-8\");",
    "  char* buf = (char*)malloc(b.len + 1);",
    "  if (!buf && b.len > 0) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  if (b.len > 0) memcpy(buf, b.ptr, b.len);",
    "  buf[b.len] = 0;",
    "  ps_string out = { buf, b.len };",
    "  return out;",
    "}",
    "static ps_string ps_i64_to_string(int64_t v) {",
    "  static char buf[4][64];",
    "  static int slot = 0;",
    "  slot = (slot + 1) & 3;",
    "  int n = snprintf(buf[slot], sizeof(buf[slot]), \"%lld\", (long long)v);",
    "  if (n < 0) n = 0;",
    "  ps_string s = { buf[slot], (size_t)n };",
    "  return s;",
    "}",
    "static ps_string ps_u32_to_string(uint32_t v) {",
    "  static char buf[4][64];",
    "  static int slot = 0;",
    "  slot = (slot + 1) & 3;",
    "  int n = snprintf(buf[slot], sizeof(buf[slot]), \"%u\", (unsigned)v);",
    "  if (n < 0) n = 0;",
    "  ps_string s = { buf[slot], (size_t)n };",
    "  return s;",
    "}",
  ];
}

function emitFunctionPrototypes(ir) {
  return ir.functions.map((fn) => {
    const ret = fn.name === "main" ? "int" : cTypeFromName(fn.returnType.name);
    const params = fn.params.map((p) => `${cTypeFromName(p.type.name)} ${cIdent(p.name)}`).join(", ");
    return `${ret} ${cIdent(fn.name)}(${params || "void"});`;
  });
}

function emitInstr(i, fnInf, state) {
  const t = (v) => fnInf.tempTypes.get(v) || fnInf.varTypes.get(v) || "int";
  const n = (v) => cIdent(v);
  const out = [];
  const aliases = state?.mapAliases;
  const varAliases = state?.mapVarAliases;
  const resolveVarAlias = (name) => {
    if (!varAliases || !name) return name;
    let cur = name;
    const seen = new Set();
    while (varAliases.has(cur) && !seen.has(cur)) {
      seen.add(cur);
      const next = varAliases.get(cur);
      if (!next) break;
      cur = next;
    }
    return cur;
  };
  const aliasOf = (tmp) => (aliases ? aliases.get(tmp) : undefined);
  const setAlias = (tmp, name) => {
    if (!aliases || !tmp) return;
    if (name) aliases.set(tmp, resolveVarAlias(name));
    else aliases.delete(tmp);
  };
  const setVarAlias = (name, target) => {
    if (!varAliases || !name) return;
    if (target) varAliases.set(name, resolveVarAlias(target));
    else varAliases.set(name, name);
  };
  switch (i.op) {
    case "const":
      if (i.literalType === "string") {
        out.push(`${n(i.dst)}.ptr = ${JSON.stringify(String(i.value))};`);
        out.push(`${n(i.dst)}.len = strlen(${n(i.dst)}.ptr);`);
      } else if (i.literalType === "bool") {
        out.push(`${n(i.dst)} = ${(String(i.value) === "true") ? "true" : "false"};`);
      } else {
        out.push(`${n(i.dst)} = ${i.value};`);
      }
      break;
    case "var_decl":
      if (isAliasTrackedType(i.type.name)) setVarAlias(i.name, i.name);
      out.push(`/* var_decl ${n(i.name)}:${i.type.name} */`);
      break;
    case "load_var":
      setAlias(i.dst, isAliasTrackedType(i.type.name) ? resolveVarAlias(i.name) : null);
      out.push(`${n(i.dst)} = ${n(i.name)};`);
      break;
    case "store_var":
      if (isAliasTrackedType(i.type.name || t(i.name))) setVarAlias(i.name, aliasOf(i.src) || null);
      out.push(`${n(i.name)} = ${n(i.src)};`);
      break;
    case "check_int_overflow":
      if (i.operator === "+") out.push(`ps_check_int_overflow_add(${n(i.left)}, ${n(i.right)});`);
      else if (i.operator === "-") out.push(`ps_check_int_overflow_sub(${n(i.left)}, ${n(i.right)});`);
      else if (i.operator === "*") out.push(`ps_check_int_overflow_mul(${n(i.left)}, ${n(i.right)});`);
      break;
    case "check_div_zero":
      out.push(`ps_check_div_zero_int(${n(i.divisor)});`);
      break;
    case "check_shift_range":
      out.push(`ps_check_shift_range(${n(i.shift)}, ${i.width});`);
      break;
    case "check_index_bounds":
      if (t(i.target) === "string") out.push(`ps_check_index_bounds(ps_utf8_glyph_len(${n(i.target)}), ${n(i.index)});`);
      else out.push(`ps_check_index_bounds(${n(i.target)}.len, ${n(i.index)});`);
      break;
    case "check_map_has_key":
      {
        const m = parseMapType(t(i.map));
        const mapRefName = aliasOf(i.map) || i.map;
        if (m) out.push(`ps_check_map_has_key(ps_map_has_key_${m.base}(&${n(mapRefName)}, ${n(i.key)}));`);
        else out.push("ps_check_map_has_key(0);");
      }
      break;
    case "bin_op":
      out.push(`${n(i.dst)} = (${n(i.left)} ${i.operator} ${n(i.right)});`);
      break;
    case "unary_op":
      out.push(`${n(i.dst)} = (${i.operator}${n(i.src)});`);
      break;
    case "copy":
      setAlias(i.dst, aliasOf(i.src) || null);
      out.push(`${n(i.dst)} = ${n(i.src)};`);
      break;
    case "postfix_op":
      out.push(`${n(i.dst)} = ${n(i.src)}${i.operator};`);
      break;
    case "call_static":
      if (t(i.dst) === "void") out.push(`${cIdent(i.callee)}(${i.args.map(n).join(", ")});`);
      else out.push(`${n(i.dst)} = ${cIdent(i.callee)}(${i.args.map(n).join(", ")});`);
      break;
    case "call_method_static":
      {
        const rt = t(i.receiver);
        const rc = parseContainer(rt);
        if (i.method === "length") {
          if (rt === "string") out.push(`${n(i.dst)} = (int64_t)ps_utf8_glyph_len(${n(i.receiver)});`);
          else if (rc && ["list", "slice", "view"].includes(rc.kind)) out.push(`${n(i.dst)} = (int64_t)${n(i.receiver)}.len;`);
          else if (rc && rc.kind === "map") out.push(`${n(i.dst)} = (int64_t)${n(i.receiver)}.len;`);
          else out.push(`${n(i.dst)} = 0;`);
        } else if (rt === "int" && i.method === "toByte") {
          out.push(`${n(i.dst)} = (uint8_t)${n(i.receiver)};`);
        } else if (rt === "string" && i.method === "substring") {
          out.push(`${n(i.dst)} = ps_string_substring(${n(i.receiver)}, ${n(i.args[0])}, ${n(i.args[1])});`);
        } else if (rt === "string" && i.method === "indexOf") {
          out.push(`${n(i.dst)} = ps_string_index_of(${n(i.receiver)}, ${n(i.args[0])});`);
        } else if (rt === "string" && i.method === "startsWith") {
          out.push(`${n(i.dst)} = ps_string_starts_with(${n(i.receiver)}, ${n(i.args[0])});`);
        } else if (rt === "string" && i.method === "endsWith") {
          out.push(`${n(i.dst)} = ps_string_ends_with(${n(i.receiver)}, ${n(i.args[0])});`);
        } else if (rt === "string" && i.method === "split") {
          out.push(`${n(i.dst)} = ps_string_split(${n(i.receiver)}, ${n(i.args[0])});`);
        } else if (rt === "string" && i.method === "trim") {
          out.push(`${n(i.dst)} = ps_string_trim(${n(i.receiver)}, 0);`);
        } else if (rt === "string" && i.method === "trimStart") {
          out.push(`${n(i.dst)} = ps_string_trim(${n(i.receiver)}, 1);`);
        } else if (rt === "string" && i.method === "trimEnd") {
          out.push(`${n(i.dst)} = ps_string_trim(${n(i.receiver)}, 2);`);
        } else if (rt === "string" && i.method === "replace") {
          out.push(`${n(i.dst)} = ps_string_replace(${n(i.receiver)}, ${n(i.args[0])}, ${n(i.args[1])});`);
        } else if (rt === "string" && i.method === "toUpper") {
          out.push(`${n(i.dst)} = ps_string_to_upper(${n(i.receiver)});`);
        } else if (rt === "string" && i.method === "toLower") {
          out.push(`${n(i.dst)} = ps_string_to_lower(${n(i.receiver)});`);
        } else if (rt === "string" && i.method === "concat") {
          out.push(`${n(i.dst)} = ps_string_concat(${n(i.receiver)}, ${n(i.args[0])});`);
        } else if (rt === "string" && i.method === "toUtf8Bytes") {
          out.push(`${n(i.dst)} = ps_string_to_utf8_bytes(${n(i.receiver)});`);
        } else if (rc && rc.kind === "list" && rc.inner === "byte" && i.method === "toUtf8String") {
          out.push(`${n(i.dst)} = ps_list_byte_to_utf8_string(${n(i.receiver)});`);
        } else if (i.method === "pop" && rc && rc.kind === "list") {
          const recv = aliasOf(i.receiver) || i.receiver;
          out.push(`if (${n(recv)}.len == 0) ps_panic("R1006", "RUNTIME_EMPTY_POP", "pop on empty list");`);
          out.push(`${n(recv)}.len -= 1;`);
          out.push(`${n(i.dst)} = ${n(recv)}.ptr[${n(recv)}.len];`);
        } else {
          out.push(`/* static method call */ ${n(i.dst)} = 0; /* ${i.method} */`);
        }
      }
      break;
    case "call_builtin_print":
      if (i.args.length > 0) out.push(`printf("%s\\n", ${n(i.args[0])}.ptr);`);
      else out.push('printf("\\n");');
      break;
    case "call_builtin_tostring":
      {
        const vt = t(i.value);
        if (vt === "string") out.push(`${n(i.dst)} = ${n(i.value)};`);
        else if (vt === "int") out.push(`${n(i.dst)} = ps_i64_to_string(${n(i.value)});`);
        else if (vt === "byte" || vt === "glyph") out.push(`${n(i.dst)} = ps_u32_to_string((uint32_t)${n(i.value)});`);
        else if (vt === "bool") {
          out.push(`${n(i.dst)}.ptr = ${n(i.value)} ? "true" : "false";`);
          out.push(`${n(i.dst)}.len = ${n(i.value)} ? 4 : 5;`);
        } else {
          out.push(`${n(i.dst)}.ptr = "<toString>";`);
          out.push(`${n(i.dst)}.len = 10;`);
        }
      }
      break;
    case "make_view":
      out.push(`${n(i.dst)}.ptr = ${n(i.source)}.ptr;`);
      out.push(`${n(i.dst)}.len = ${n(i.len)};`);
      break;
    case "index_get":
      {
        const tt = t(i.target);
        const m = parseMapType(tt);
        const mapRefName = aliasOf(i.target) || i.target;
        if (m) {
          out.push(`${n(i.dst)} = ps_map_get_${m.base}(&${n(mapRefName)}, ${n(i.index)});`);
        } else if (tt === "string") {
          out.push(`${n(i.dst)} = ps_string_index_glyph(${n(i.target)}, ${n(i.index)});`);
        } else {
          out.push(`${n(i.dst)} = ${n(i.target)}.ptr[${n(i.index)}];`);
        }
      }
      break;
    case "index_set":
      {
        const tt = t(i.target);
        const m = parseMapType(tt);
        const mapRefName = aliasOf(i.target) || i.target;
        if (m) {
          out.push(`ps_map_set_${m.base}(&${n(mapRefName)}, ${n(i.index)}, ${n(i.src)});`);
        } else {
          out.push(`${n(i.target)}.ptr[${n(i.index)}] = ${n(i.src)};`);
        }
      }
      break;
    case "make_list":
      out.push(`${n(i.dst)}.len = ${i.items.length};`);
      out.push(`${n(i.dst)}.cap = ${i.items.length};`);
      out.push(
        `${n(i.dst)}.ptr = (${cTypeFromName(parseContainer(t(i.dst))?.inner || "int")}*)malloc(sizeof(*${n(i.dst)}.ptr) * ${i.items.length});`
      );
      i.items.forEach((it, idx) => out.push(`${n(i.dst)}.ptr[${idx}] = ${n(it)};`));
      break;
    case "make_map":
      out.push(`${n(i.dst)}.keys = NULL;`);
      out.push(`${n(i.dst)}.values = NULL;`);
      out.push(`${n(i.dst)}.len = 0;`);
      out.push(`${n(i.dst)}.cap = 0;`);
      {
        const m = parseMapType(t(i.dst));
        if (m) {
          i.pairs.forEach((p) => out.push(`ps_map_set_${m.base}(&${n(i.dst)}, ${n(p.key)}, ${n(p.value)});`));
        }
      }
      break;
    case "member_get":
      out.push(`/* member_get ${i.name} */ ${n(i.dst)} = 0;`);
      break;
    case "ret":
      out.push(`return ${n(i.value)};`);
      break;
    case "ret_void":
      out.push("return;");
      break;
    case "throw":
      out.push(`ps_panic("R1999", "RUNTIME_THROW", "explicit throw");`);
      break;
    case "branch_if":
      out.push(`if (${n(i.cond)}) goto ${i.then}; else goto ${i.else};`);
      break;
    case "jump":
      out.push(`goto ${i.target};`);
      break;
    case "iter_begin":
      out.push(`${n(i.dst)}.i = 0;`);
      out.push(`${n(i.dst)}.n = ${n(i.source)}.len;`);
      break;
    case "branch_iter_has_next":
      out.push(`if (${n(i.iter)}.i < ${n(i.iter)}.n) goto ${i.then}; else goto ${i.else};`);
      break;
    case "iter_next":
      {
        const srcType = t(i.source);
        const m = parseMapType(srcType);
        if (m && i.mode === "in") out.push(`${n(i.dst)} = ${n(i.source)}.keys[${n(i.iter)}.i];`);
        else if (m && i.mode === "of") out.push(`${n(i.dst)} = ${n(i.source)}.values[${n(i.iter)}.i];`);
        else if (srcType === "string") out.push(`${n(i.dst)} = (uint8_t)${n(i.source)}.ptr[${n(i.iter)}.i];`);
        else out.push(`${n(i.dst)} = ${n(i.source)}.ptr[${n(i.iter)}.i];`);
      }
      out.push(`${n(i.iter)}.i += 1;`);
      break;
    case "select":
      out.push(`${n(i.dst)} = (${n(i.cond)}) ? (${n(i.thenValue)}) : (${n(i.elseValue)});`);
      break;
    case "nop":
      out.push(";");
      break;
    case "break":
      out.push("/* break in lowered CFG */");
      break;
    case "continue":
      out.push("/* continue in lowered CFG */");
      break;
    default:
      out.push(`/* unhandled IR op: ${i.op} */`);
      break;
  }
  return out;
}

function emitFunctionBody(fn, fnInf) {
  const out = [];
  const ret = fn.name === "main" ? "int" : cTypeFromName(fn.returnType.name);
  const params = fn.params.map((p) => `${cTypeFromName(p.type.name)} ${cIdent(p.name)}`).join(", ");
  out.push(`${ret} ${cIdent(fn.name)}(${params || "void"}) {`);

  const varDecls = Array.from(fnInf.varTypes.entries()).sort((a, b) => a[0].localeCompare(b[0]));
  for (const [name, typeName] of varDecls) {
    if (fn.params.some((p) => p.name === name)) continue;
    out.push(`  ${cTypeFromName(typeName)} ${cIdent(name)};`);
  }

  const tempDecls = [];
  const temps = Array.from(fnInf.tempTypes.entries()).sort((a, b) => a[0].localeCompare(b[0]));
  for (const [name, typeName] of temps) {
    if (typeName === "void") continue;
    tempDecls.push(`  ${cTypeFromName(typeName)} ${cIdent(name)};`);
  }
  if (tempDecls.length > 0) out.push(...tempDecls);

  const emitState = { mapAliases: new Map(), mapVarAliases: new Map() };
  if (fn.blocks.length > 0) out.push(`  goto ${fn.blocks[0].label};`);
  for (const b of fn.blocks) {
    out.push(`${b.label}:`);
    for (const i of b.instrs) {
      const lines = emitInstr(i, fnInf, emitState);
      for (const l of lines) out.push(`  ${l}`);
    }
  }

  if (ret === "void") out.push("  return;");
  else if (fn.name === "main") out.push("  return 0;");
  else out.push(`  return (${ret}){0};`);
  out.push("}");
  return out;
}

function generateC(ir) {
  const inferred = inferTempTypes(ir);
  const typeNames = collectTypeNames(ir, inferred);
  const out = [];
  out.push("/* ProtoScript V2 reference C backend (non-optimized) */");
  out.push("/* This C is intended as a semantic oracle. */");
  out.push("#include <stdbool.h>");
  out.push("#include <stddef.h>");
  out.push("#include <stdint.h>");
  out.push("#include <stdio.h>");
  out.push("#include <stdlib.h>");
  out.push("#include <string.h>");
  out.push("");
  out.push(...emitTypeDecls(typeNames));
  out.push("");
  out.push(...emitRuntimeHelpers());
  out.push("");
  out.push(...emitContainerHelpers(typeNames));
  out.push("");
  out.push(...emitFunctionPrototypes(ir));
  out.push("");
  for (const fn of ir.functions) {
    out.push(...emitFunctionBody(fn, inferred.get(fn.name)));
    out.push("");
  }
  return `${out.join("\n")}\n`;
}

function cIdent(name) {
  if (typeof name !== "string") return String(name);
  const cleaned = name.replace(/[^A-Za-z0-9_]/g, "_");
  if (/^[0-9]/.test(cleaned)) return `v_${cleaned}`;
  return cleaned;
}

module.exports = {
  generateC,
};
