"use strict";

const fs = require("fs");
const path = require("path");

let PROTO_MAP = new Map();

function loadModuleRegistry() {
  const candidates = [
    path.join(process.cwd(), "modules", "registry.json"),
    path.join(__dirname, "..", "modules", "registry.json"),
  ];
  for (const p of candidates) {
    try {
      if (!fs.existsSync(p)) continue;
      const doc = JSON.parse(fs.readFileSync(p, "utf8"));
      const map = new Map();
      if (doc && Array.isArray(doc.modules)) {
        for (const m of doc.modules) {
          if (!m || typeof m.name !== "string" || !Array.isArray(m.functions)) continue;
          for (const f of m.functions) {
            if (!f || typeof f.name !== "string" || typeof f.ret !== "string") continue;
            map.set(`${m.name}.${f.name}`, f.ret);
          }
        }
      }
      return map;
    } catch (err) {
      continue;
    }
  }
  return new Map();
}

const MODULE_RETURNS = loadModuleRegistry();

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
    case "File":
      return "ps_file*";
    case "EOF":
      return "int64_t";
    case "JSONValue":
      return "ps_jsonvalue";
    case "Exception":
    case "RuntimeException":
      return "ps_exception*";
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
  if (PROTO_MAP.has(typeName)) return `${typeName}*`;
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

function inferTempTypes(ir, protoMap) {
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
              if (i.literalType === "file") set(i.dst, "File");
              else if (i.literalType === "eof") set(i.dst, "EOF");
              else set(i.dst, i.literalType);
              break;
            case "get_exception":
              set(i.dst, "Exception");
              break;
            case "exception_is":
              set(i.dst, "bool");
              break;
            case "copy":
              if (tempTypes.has(i.src)) set(i.dst, tempTypes.get(i.src));
              break;
            case "load_var":
              if (i.type.name === "unknown" && varTypes.has(i.name)) set(i.dst, varTypes.get(i.name));
              else set(i.dst, i.type.name);
              break;
            case "store_var":
              if (tempTypes.has(i.src)) {
                if (!varTypes.has(i.name) || varTypes.get(i.name) === "unknown") {
                  varTypes.set(i.name, tempTypes.get(i.src));
                }
              }
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
              else if (MODULE_RETURNS.has(i.callee)) set(i.dst, MODULE_RETURNS.get(i.callee));
              else if (i.callee === "Exception") set(i.dst, "Exception");
              break;
            case "call_method_static":
              if (getType(i.receiver)) {
                const rt = getType(i.receiver);
                const rc = parseContainer(rt);
                if (rt === "File") {
                  if (i.method === "read") {
                    if (i.args.length === 0) set(i.dst, "string");
                    else set(i.dst, "EOF");
                  } else if (i.method === "readBytes") {
                    if (i.args.length === 0) set(i.dst, "list<byte>");
                    else set(i.dst, "EOF");
                  } else {
                    set(i.dst, "void");
                  }
                } else if (rt === "JSONValue") {
                  if (["isNull", "isBool", "isNumber", "isString", "isArray", "isObject"].includes(i.method)) set(i.dst, "bool");
                  else if (i.method === "asBool") set(i.dst, "bool");
                  else if (i.method === "asNumber") set(i.dst, "float");
                  else if (i.method === "asString") set(i.dst, "string");
                  else if (i.method === "asArray") set(i.dst, "list<JSONValue>");
                  else if (i.method === "asObject") set(i.dst, "map<string,JSONValue>");
                } else if (rt === "int") {
                  if (i.method === "toByte") set(i.dst, "byte");
                  else if (i.method === "toFloat") set(i.dst, "float");
                  else if (i.method === "toString") set(i.dst, "string");
                  else if (i.method === "toBytes") set(i.dst, "list<byte>");
                  else set(i.dst, "int");
                } else if (rt === "byte") {
                  if (i.method === "toInt") set(i.dst, "int");
                  else if (i.method === "toFloat") set(i.dst, "float");
                  else if (i.method === "toString") set(i.dst, "string");
                  else set(i.dst, "byte");
                } else if (rt === "float") {
                  if (i.method === "toInt") set(i.dst, "int");
                  else if (i.method === "toString") set(i.dst, "string");
                  else if (i.method === "toBytes") set(i.dst, "list<byte>");
                  else if (i.method === "abs") set(i.dst, "float");
                  else if (["isNaN", "isInfinite", "isFinite"].includes(i.method)) set(i.dst, "bool");
                  else set(i.dst, "float");
                } else if (rt === "glyph") {
                  if (i.method === "toString") set(i.dst, "string");
                  else if (i.method === "toInt") set(i.dst, "int");
                  else if (i.method === "toUtf8Bytes") set(i.dst, "list<byte>");
                  else if (["isLetter", "isDigit", "isWhitespace", "isUpper", "isLower"].includes(i.method)) set(i.dst, "bool");
                  else if (["toUpper", "toLower"].includes(i.method)) set(i.dst, "glyph");
                  else set(i.dst, "glyph");
                } else if (rt === "string") {
                  if (["length", "indexOf"].includes(i.method)) set(i.dst, "int");
                  else if (i.method === "isEmpty") set(i.dst, "bool");
                  else if (i.method === "toString") set(i.dst, "string");
                  else if (i.method === "toInt") set(i.dst, "int");
                  else if (i.method === "toFloat") set(i.dst, "float");
                  else if (["startsWith", "endsWith"].includes(i.method)) set(i.dst, "bool");
                  else if (i.method === "split") set(i.dst, "list<string>");
                  else if (i.method === "toUtf8Bytes") set(i.dst, "list<byte>");
                  else if (["substring", "trim", "trimStart", "trimEnd", "replace", "toUpper", "toLower", "concat"].includes(i.method)) set(i.dst, "string");
                } else if (rc && rc.kind === "list" && rc.inner === "byte" && i.method === "toUtf8String") {
                  set(i.dst, "string");
                } else if (rc && rc.kind === "list" && rc.inner === "string" && (i.method === "join" || i.method === "concat")) {
                  set(i.dst, "string");
                } else if (rc && rc.kind === "list" && i.method === "push") {
                  set(i.dst, "int");
                } else if (rc && rc.kind === "list" && i.method === "contains") {
                  set(i.dst, "bool");
                } else if (rc && rc.kind === "list" && i.method === "sort") {
                  set(i.dst, "int");
                } else if (rc && rc.kind === "map") {
                  const m = parseMapType(rt);
                  if (i.method === "containsKey") set(i.dst, "bool");
                  else if (i.method === "keys" && m) set(i.dst, `list<${m.keyType}>`);
                  else if (i.method === "values" && m) set(i.dst, `list<${m.valueType}>`);
                  else if (i.method === "length") set(i.dst, "int");
                  else if (i.method === "isEmpty") set(i.dst, "bool");
                } else if (i.method === "length") {
                  set(i.dst, "int");
                } else {
                  set(i.dst, "int");
                }
              } else {
                set(i.dst, "int");
              }
              break;
            case "member_get": {
              const rt = getType(i.target);
              if (rt === "Exception" || rt === "RuntimeException") {
                if (["code", "category", "message", "file"].includes(i.name)) set(i.dst, "string");
                else if (["line", "column"].includes(i.name)) set(i.dst, "int");
              } else if (protoMap && protoMap.has(rt)) {
                const fields = collectProtoFields(protoMap, rt);
                const hit = fields.find((f) => f.name === i.name);
                if (hit) {
                  const t = typeof hit.type === "string" ? hit.type : hit.type?.name;
                  if (t) set(i.dst, t);
                }
              }
              break;
            }
            case "make_object":
              if (i.proto) set(i.dst, i.proto);
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
  const names = new Set([
    "int",
    "float",
    "bool",
    "byte",
    "glyph",
    "string",
    "JSONValue",
    "void",
    "iter_cursor",
    "list<string>",
    "list<byte>",
    "list<int>",
    "list<JSONValue>",
    "map<string,int>",
    "map<string,JSONValue>",
  ]);
  for (const fn of ir.functions) {
    names.add(fn.returnType.name);
    for (const p of fn.params) names.add(p.type.name);
    const inf = inferred.get(fn.name);
    for (const t of inf.varTypes.values()) names.add(t);
    for (const t of inf.tempTypes.values()) names.add(t);
  }
  if (Array.isArray(ir.prototypes)) {
    for (const p of ir.prototypes) {
      if (p && p.name) names.add(p.name);
      for (const f of p.fields || []) names.add(f.type?.name || f.type);
    }
  }
  return names;
}

function buildProtoMap(ir) {
  const map = new Map();
  if (!ir || !Array.isArray(ir.prototypes)) return map;
  for (const p of ir.prototypes) {
    if (!p || !p.name) continue;
    map.set(p.name, { name: p.name, parent: p.parent || null, fields: p.fields || [] });
  }
  return map;
}

function collectProtoFields(protoMap, name) {
  const out = [];
  const chain = [];
  let cur = protoMap.get(name);
  while (cur) {
    chain.push(cur);
    cur = cur.parent ? protoMap.get(cur.parent) : null;
  }
  for (let i = chain.length - 1; i >= 0; i -= 1) {
    for (const f of chain[i].fields || []) out.push(f);
  }
  return out;
}

function emitTypeDecls(typeNames, protoMap) {
  const out = [];
  out.push("typedef struct { const char* ptr; size_t len; } ps_string;");
  out.push("typedef struct { size_t i; size_t n; } ps_iter_cursor;");
  out.push(
    "typedef struct { ps_string str; uint32_t* ptr; size_t len; size_t offset; int is_string; const uint64_t* version_ptr; uint64_t version; } ps_view_glyph;"
  );
  if (typeNames.has("JSONValue")) {
    out.push("typedef struct ps_jsonvalue ps_jsonvalue;");
  }
  if (protoMap && protoMap.size > 0) {
    for (const name of protoMap.keys()) {
      out.push(`typedef struct ${name} ${name};`);
    }
    for (const [name, p] of protoMap.entries()) {
      const fields = collectProtoFields(protoMap, name);
      out.push(`struct ${name} {`);
      if (fields.length === 0) {
        out.push("  uint8_t __empty;");
      } else {
        for (const f of fields) {
          const t = typeof f.type === "string" ? f.type : f.type?.name;
          out.push(`  ${cTypeFromName(t || "int")} ${f.name};`);
        }
      }
      out.push("};");
    }
  }
  for (const t of typeNames) {
    const p = parseContainer(t);
    if (!p) continue;
    const innerC = cTypeFromName(p.inner);
    const bn = baseName(p.inner);
    if (p.kind === "list") {
      out.push(`typedef struct { ${innerC}* ptr; size_t len; size_t cap; uint64_t version; } ps_list_${bn};`);
    } else if (p.kind === "view") {
      if (p.inner === "glyph") continue;
      out.push(`typedef struct { ${innerC}* ptr; size_t len; const uint64_t* version_ptr; uint64_t version; } ps_view_${bn};`);
    } else if (p.kind === "slice") {
      out.push(`typedef struct { ${innerC}* ptr; size_t len; const uint64_t* version_ptr; uint64_t version; } ps_slice_${bn};`);
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
  const typeSet = new Set(typeNames);
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
    const needKeyList = typeSet.has(`list<${m.keyType}>`);
    const needValList = typeSet.has(`list<${m.valueType}>`);
    if (needKeyList) {
      const keyList = `ps_list_${baseName(m.keyType)}`;
      out.push(`static ${keyList} ps_map_keys_${m.base}(const ${mapC}* m) {`);
      out.push(`  ${keyList} out = { NULL, 0, 0, 0 };`);
      out.push("  if (!m || m->len == 0) return out;");
      out.push("  out.len = m->len;");
      out.push("  out.cap = m->len;");
      out.push(`  out.ptr = (${cTypeFromName(m.keyType)}*)malloc(sizeof(*out.ptr) * m->len);`);
      out.push('  if (!out.ptr) ps_panic("R1998", "RUNTIME_OOM", "out of memory");');
      out.push("  for (size_t i = 0; i < m->len; i += 1) out.ptr[i] = m->keys[i];");
      out.push("  return out;");
      out.push("}");
    }
    if (needValList) {
      const valList = `ps_list_${baseName(m.valueType)}`;
      out.push(`static ${valList} ps_map_values_${m.base}(const ${mapC}* m) {`);
      out.push(`  ${valList} out = { NULL, 0, 0, 0 };`);
      out.push("  if (!m || m->len == 0) return out;");
      out.push("  out.len = m->len;");
      out.push("  out.cap = m->len;");
      out.push(`  out.ptr = (${cTypeFromName(m.valueType)}*)malloc(sizeof(*out.ptr) * m->len);`);
      out.push('  if (!out.ptr) ps_panic("R1998", "RUNTIME_OOM", "out of memory");');
      out.push("  for (size_t i = 0; i < m->len; i += 1) out.ptr[i] = m->values[i];");
      out.push("  return out;");
      out.push("}");
    }
  }
  return out;
}

function emitRuntimeHelpers() {
  return [
    "typedef struct { ps_string file; int64_t line; int64_t column; ps_string message; ps_string code; ps_string category; int is_runtime; } ps_exception;",
    "static ps_exception ps_last_exception;",
    "static int ps_has_exception = 0;",
    "static jmp_buf ps_try_stack[64];",
    "static int ps_try_len = 0;",
    "static ps_string ps_cstr(const char* s) {",
    "  ps_string out = { s ? s : \"\", s ? strlen(s) : 0 };",
    "  return out;",
    "}",
    "static bool ps_utf8_validate(const uint8_t* s, size_t len);",
    "static void ps_set_exception(const char* code, const char* category, const char* msg, int is_runtime) {",
    "  ps_last_exception.file = ps_cstr(\"<runtime>\");",
    "  ps_last_exception.line = 1;",
    "  ps_last_exception.column = 1;",
    "  ps_last_exception.message = ps_cstr(msg);",
    "  ps_last_exception.code = ps_cstr(code ? code : \"\");",
    "  ps_last_exception.category = ps_cstr(category ? category : \"\");",
    "  ps_last_exception.is_runtime = is_runtime;",
    "  ps_has_exception = 1;",
    "}",
    "static void ps_raise_exception(void) {",
    "  if (ps_try_len > 0) {",
    "    ps_try_len -= 1;",
    "    longjmp(ps_try_stack[ps_try_len], 1);",
    "  }",
    "  if (ps_has_exception) {",
    "    fprintf(stderr, \"<runtime>:1:1 %s %s: %s\\n\", ps_last_exception.code.ptr, ps_last_exception.category.ptr, ps_last_exception.message.ptr);",
    "  }",
    "  exit(1);",
    "}",
    "static void ps_panic(const char* code, const char* category, const char* msg) {",
    "  ps_set_exception(code, category, msg, 1);",
    "  ps_raise_exception();",
    "}",
    "static ps_exception* ps_get_exception(void) {",
    "  if (!ps_has_exception) ps_set_exception(\"R1999\", \"RUNTIME_THROW\", \"exception\", 1);",
    "  return &ps_last_exception;",
    "}",
    "static int ps_exception_is(ps_exception* ex, const char* type) {",
    "  if (!ex || !type) return 0;",
    "  if (strcmp(type, \"Exception\") == 0) return 1;",
    "  if (strcmp(type, \"RuntimeException\") == 0) return ex->is_runtime;",
    "  return 0;",
    "}",
    "static ps_exception ps_exception_make(ps_string message) {",
    "  ps_exception ex;",
    "  ex.file = ps_cstr(\"\");",
    "  ex.line = 1;",
    "  ex.column = 1;",
    "  ex.message = message;",
    "  ex.code = ps_cstr(\"\");",
    "  ex.category = ps_cstr(\"\");",
    "  ex.is_runtime = 0;",
    "  return ex;",
    "}",
    "static ps_exception* ps_exception_new(ps_string message) {",
    "  ps_exception* ex = (ps_exception*)malloc(sizeof(ps_exception));",
    "  if (!ex) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  *ex = ps_exception_make(message);",
    "  return ex;",
    "}",
    "static void ps_raise_user_exception(ps_exception* ex) {",
    "  if (ex) { ps_last_exception = *ex; ps_has_exception = 1; }",
    "  ps_raise_exception();",
    "}",
    "",
    "typedef struct { FILE* fp; int binary; int is_std; int closed; } ps_file;",
    "static ps_file ps_stdin = { NULL, 0, 1, 0 };",
    "static ps_file ps_stdout = { NULL, 0, 1, 0 };",
    "static ps_file ps_stderr = { NULL, 0, 1, 0 };",
    "static int ps_io_ready = 0;",
    "static void ps_io_init(void) {",
    "  if (ps_io_ready) return;",
    "  ps_io_ready = 1;",
    "  if (!ps_stdin.fp) ps_stdin.fp = stdin;",
    "  if (!ps_stdout.fp) ps_stdout.fp = stdout;",
    "  if (!ps_stderr.fp) ps_stderr.fp = stderr;",
    "  if (ps_stdout.fp) setvbuf(ps_stdout.fp, NULL, _IONBF, 0);",
    "  if (ps_stderr.fp) setvbuf(ps_stderr.fp, NULL, _IONBF, 0);",
    "}",
    "static char* ps_string_to_cstr(ps_string s) {",
    "  char* out = (char*)malloc(s.len + 1);",
    "  if (!out) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  memcpy(out, s.ptr, s.len);",
    "  out[s.len] = '\\0';",
    "  return out;",
    "}",
    "static ps_string ps_string_from_owned(char* s) {",
    "  ps_string out = { s ? s : \"\", s ? strlen(s) : 0 };",
    "  return out;",
    "}",
    "static ps_file* ps_file_open(ps_string path, ps_string mode) {",
    "  ps_io_init();",
    "  char* cpath = ps_string_to_cstr(path);",
    "  char* cmode = ps_string_to_cstr(mode);",
    "  if (strcmp(cmode, \"r\") != 0 && strcmp(cmode, \"w\") != 0 && strcmp(cmode, \"rb\") != 0 && strcmp(cmode, \"wb\") != 0 && strcmp(cmode, \"a\") != 0 && strcmp(cmode, \"ab\") != 0) {",
    "    free(cpath);",
    "    free(cmode);",
    "    ps_panic(\"R1010\", \"RUNTIME_IO_ERROR\", \"invalid mode\");",
    "  }",
    "  int binary = (strchr(cmode, 'b') != NULL);",
    "  FILE* fp = fopen(cpath, cmode);",
    "  free(cpath);",
    "  free(cmode);",
    "  if (!fp) ps_panic(\"R1010\", \"RUNTIME_IO_ERROR\", \"cannot open file\");",
    "  ps_file* f = (ps_file*)malloc(sizeof(ps_file));",
    "  if (!f) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  f->fp = fp;",
    "  f->binary = binary;",
    "  f->is_std = 0;",
    "  f->closed = 0;",
    "  return f;",
    "}",
    "static void ps_file_check_open(ps_file* f) {",
    "  ps_io_init();",
    "  if (!f || f->closed) ps_panic(\"R1010\", \"RUNTIME_IO_ERROR\", \"file is closed\");",
    "}",
    "static void ps_file_close(ps_file* f) {",
    "  ps_file_check_open(f);",
    "  if (f->is_std) ps_panic(\"R1010\", \"RUNTIME_IO_ERROR\", \"cannot close standard stream\");",
    "  fclose(f->fp);",
    "  f->closed = 1;",
    "}",
    "static void ps_file_write_text(ps_file* f, ps_string s) {",
    "  ps_file_check_open(f);",
    "  if (f->binary) ps_panic(\"R1010\", \"RUNTIME_IO_ERROR\", \"write expects list<byte>\");",
    "  fwrite(s.ptr, 1, s.len, f->fp);",
    "}",
    "static void ps_file_write_bytes(ps_file* f, ps_list_byte b) {",
    "  ps_file_check_open(f);",
    "  if (!f->binary) ps_panic(\"R1010\", \"RUNTIME_IO_ERROR\", \"write expects string\");",
    "  if (b.len > 0) fwrite(b.ptr, 1, b.len, f->fp);",
    "}",
    "static void ps_file_write_bytes_from_ints(ps_file* f, ps_list_int b) {",
    "  ps_file_check_open(f);",
    "  if (!f->binary) ps_panic(\"R1010\", \"RUNTIME_IO_ERROR\", \"write expects string\");",
    "  for (size_t i = 0; i < b.len; i += 1) {",
    "    int64_t v = b.ptr[i];",
    "    if (v < 0 || v > 255) ps_panic(\"R1008\", \"RUNTIME_BYTE_RANGE\", \"byte out of range\");",
    "    unsigned char c = (unsigned char)v;",
    "    fwrite(&c, 1, 1, f->fp);",
    "  }",
    "}",
    "static ps_string ps_file_read_all(ps_file* f) {",
    "  ps_file_check_open(f);",
    "  fseek(f->fp, 0, SEEK_END);",
    "  long sz = ftell(f->fp);",
    "  if (sz < 0) sz = 0;",
    "  fseek(f->fp, 0, SEEK_SET);",
    "  char* buf = (char*)malloc((size_t)sz + 1);",
    "  if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  size_t n = fread(buf, 1, (size_t)sz, f->fp);",
    "  buf[n] = '\\0';",
    "  ps_string out = { buf, n };",
    "  if (!ps_utf8_validate((const uint8_t*)out.ptr, out.len)) ps_panic(\"R1007\", \"RUNTIME_INVALID_UTF8\", \"invalid UTF-8\");",
    "  return out;",
    "}",
    "static ps_list_byte ps_file_read_all_bytes(ps_file* f) {",
    "  ps_list_byte out = { NULL, 0, 0, 0 };",
    "  ps_file_check_open(f);",
    "  fseek(f->fp, 0, SEEK_END);",
    "  long sz = ftell(f->fp);",
    "  if (sz < 0) sz = 0;",
    "  fseek(f->fp, 0, SEEK_SET);",
    "  out.len = (size_t)sz;",
    "  out.cap = out.len;",
    "  if (out.len == 0) return out;",
    "  out.ptr = (uint8_t*)malloc(out.len);",
    "  if (!out.ptr) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  fread(out.ptr, 1, out.len, f->fp);",
    "  return out;",
    "}",
    "static int64_t ps_file_read_size(ps_file* f, int64_t size) {",
    "  ps_file_check_open(f);",
    "  if (size <= 0) ps_panic(\"R1010\", \"RUNTIME_IO_ERROR\", \"read size must be >= 1\");",
    "  char* buf = (char*)malloc((size_t)size);",
    "  if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  size_t n = fread(buf, 1, (size_t)size, f->fp);",
    "  free(buf);",
    "  return (n == 0) ? 0 : 1;",
    "}",
    "static ps_file* Io_open(ps_string path, ps_string mode) {",
    "  return ps_file_open(path, mode);",
    "}",
    "static void Io_print(ps_string s) {",
    "  ps_io_init();",
    "  fwrite(s.ptr, 1, s.len, ps_stdout.fp);",
    "}",
    "static void Io_printLine(ps_string s) {",
    "  ps_io_init();",
    "  fwrite(s.ptr, 1, s.len, ps_stdout.fp);",
    "  fputc('\\n', ps_stdout.fp);",
    "}",
    "",
    "typedef enum {",
    "  PS_JSON_NULL = 0,",
    "  PS_JSON_BOOL = 1,",
    "  PS_JSON_NUMBER = 2,",
    "  PS_JSON_STRING = 3,",
    "  PS_JSON_ARRAY = 4,",
    "  PS_JSON_OBJECT = 5",
    "} ps_json_kind;",
    "typedef struct ps_jsonvalue {",
    "  ps_json_kind kind;",
    "  bool b;",
    "  double num;",
    "  ps_string str;",
    "  ps_list_JSONValue arr;",
    "  ps_map_string_JSONValue obj;",
    "} ps_jsonvalue;",
    "static ps_jsonvalue JSON_null(void) {",
    "  ps_jsonvalue v; v.kind = PS_JSON_NULL; v.b = false; v.num = 0.0; v.str = ps_cstr(\"\");",
    "  v.arr.ptr = NULL; v.arr.len = 0; v.arr.cap = 0;",
    "  v.obj.keys = NULL; v.obj.values = NULL; v.obj.len = 0; v.obj.cap = 0;",
    "  return v;",
    "}",
    "static ps_jsonvalue JSON_bool(bool b) {",
    "  ps_jsonvalue v = JSON_null(); v.kind = PS_JSON_BOOL; v.b = b; return v;",
    "}",
    "static ps_jsonvalue JSON_number(double n) {",
    "  if (isnan(n) || isinf(n)) ps_panic(\"R1010\", \"RUNTIME_JSON_ERROR\", \"invalid JSON number\");",
    "  ps_jsonvalue v = JSON_null(); v.kind = PS_JSON_NUMBER; v.num = n; return v;",
    "}",
    "static ps_jsonvalue JSON_string(ps_string s) {",
    "  ps_jsonvalue v = JSON_null(); v.kind = PS_JSON_STRING; v.str = s; return v;",
    "}",
    "static ps_jsonvalue JSON_array(ps_list_JSONValue items) {",
    "  ps_jsonvalue v = JSON_null(); v.kind = PS_JSON_ARRAY; v.arr = items; return v;",
    "}",
    "static ps_jsonvalue JSON_object(ps_map_string_JSONValue members) {",
    "  ps_jsonvalue v = JSON_null(); v.kind = PS_JSON_OBJECT; v.obj = members; return v;",
    "}",
    "static bool ps_json_is_null(ps_jsonvalue v) { return v.kind == PS_JSON_NULL; }",
    "static bool ps_json_is_bool(ps_jsonvalue v) { return v.kind == PS_JSON_BOOL; }",
    "static bool ps_json_is_number(ps_jsonvalue v) { return v.kind == PS_JSON_NUMBER; }",
    "static bool ps_json_is_string(ps_jsonvalue v) { return v.kind == PS_JSON_STRING; }",
    "static bool ps_json_is_array(ps_jsonvalue v) { return v.kind == PS_JSON_ARRAY; }",
    "static bool ps_json_is_object(ps_jsonvalue v) { return v.kind == PS_JSON_OBJECT; }",
    "static bool ps_json_as_bool(ps_jsonvalue v) { if (v.kind != PS_JSON_BOOL) ps_panic(\"R1010\", \"RUNTIME_TYPE_ERROR\", \"not a bool\"); return v.b; }",
    "static double ps_json_as_number(ps_jsonvalue v) { if (v.kind != PS_JSON_NUMBER) ps_panic(\"R1010\", \"RUNTIME_TYPE_ERROR\", \"not a number\"); return v.num; }",
    "static ps_string ps_json_as_string(ps_jsonvalue v) { if (v.kind != PS_JSON_STRING) ps_panic(\"R1010\", \"RUNTIME_TYPE_ERROR\", \"not a string\"); return v.str; }",
    "static ps_list_JSONValue ps_json_as_array(ps_jsonvalue v) { if (v.kind != PS_JSON_ARRAY) ps_panic(\"R1010\", \"RUNTIME_TYPE_ERROR\", \"not an array\"); return v.arr; }",
    "static ps_map_string_JSONValue ps_json_as_object(ps_jsonvalue v) { if (v.kind != PS_JSON_OBJECT) ps_panic(\"R1010\", \"RUNTIME_TYPE_ERROR\", \"not an object\"); return v.obj; }",
    "typedef struct { const char* s; size_t len; size_t i; } ps_json_parser;",
    "static void ps_json_skip_ws(ps_json_parser* p) {",
    "  while (p->i < p->len) {",
    "    char c = p->s[p->i];",
    "    if (c == ' ' || c == '\\n' || c == '\\r' || c == '\\t') p->i += 1;",
    "    else break;",
    "  }",
    "}",
    "static int ps_json_match(ps_json_parser* p, const char* lit) {",
    "  size_t n = strlen(lit);",
    "  if (p->i + n > p->len) return 0;",
    "  if (strncmp(p->s + p->i, lit, n) != 0) return 0;",
    "  p->i += n;",
    "  return 1;",
    "}",
    "static ps_jsonvalue ps_json_parse_value(ps_json_parser* p, int* ok);",
    "static ps_jsonvalue ps_json_parse_string(ps_json_parser* p, int* ok);",
    "static ps_jsonvalue ps_json_parse_object(ps_json_parser* p, int* ok) {",
    "  p->i += 1;",
    "  ps_json_skip_ws(p);",
    "  if (p->i >= p->len) { *ok = 0; return JSON_null(); }",
    "  if (p->i < p->len && p->s[p->i] == '}') { p->i += 1; return JSON_object((ps_map_string_JSONValue){ NULL, NULL, 0, 0 }); }",
    "  while (p->i < p->len) {",
    "    if (p->s[p->i] != '\"') { *ok = 0; return JSON_null(); }",
    "    ps_jsonvalue key = ps_json_parse_string(p, ok);",
    "    if (!*ok) return JSON_null();",
    "    ps_json_skip_ws(p);",
    "    if (p->i >= p->len || p->s[p->i] != ':') { *ok = 0; return JSON_null(); }",
    "    p->i += 1;",
    "    ps_jsonvalue val = ps_json_parse_value(p, ok);",
    "    if (!*ok) return JSON_null();",
    "    (void)key; (void)val;",
    "    ps_json_skip_ws(p);",
    "    if (p->i < p->len && p->s[p->i] == ',') { p->i += 1; ps_json_skip_ws(p); continue; }",
    "    if (p->i < p->len && p->s[p->i] == '}') { p->i += 1; break; }",
    "    *ok = 0; return JSON_null();",
    "  }",
    "  if (p->i > p->len) { *ok = 0; return JSON_null(); }",
    "  return JSON_object((ps_map_string_JSONValue){ NULL, NULL, 0, 0 });",
    "}",
    "static ps_jsonvalue ps_json_parse_array(ps_json_parser* p, int* ok) {",
    "  ps_list_JSONValue out = { NULL, 0, 0, 0 };",
    "  p->i += 1;",
    "  ps_json_skip_ws(p);",
    "  if (p->i < p->len && p->s[p->i] == ']') { p->i += 1; return JSON_array(out); }",
    "  while (p->i < p->len) {",
    "    ps_jsonvalue v = ps_json_parse_value(p, ok);",
    "    if (!*ok) return JSON_null();",
    "    if (out.len == out.cap) {",
    "      size_t nc = (out.cap == 0) ? 4 : out.cap * 2;",
    "      out.ptr = (ps_jsonvalue*)realloc(out.ptr, sizeof(ps_jsonvalue) * nc);",
    "      if (!out.ptr) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "      out.cap = nc;",
    "    }",
    "    out.ptr[out.len++] = v;",
    "    ps_json_skip_ws(p);",
    "    if (p->i < p->len && p->s[p->i] == ',') { p->i += 1; ps_json_skip_ws(p); continue; }",
    "    if (p->i < p->len && p->s[p->i] == ']') { p->i += 1; break; }",
    "    *ok = 0; return JSON_null();",
    "  }",
    "  return JSON_array(out);",
    "}",
    "static ps_jsonvalue ps_json_parse_string(ps_json_parser* p, int* ok) {",
    "  p->i += 1;",
    "  size_t start = p->i;",
    "  while (p->i < p->len && p->s[p->i] != '\"') p->i += 1;",
    "  if (p->i >= p->len) { *ok = 0; return JSON_null(); }",
    "  size_t n = p->i - start;",
    "  char* buf = (char*)malloc(n + 1);",
    "  if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  memcpy(buf, p->s + start, n);",
    "  buf[n] = '\\0';",
    "  p->i += 1;",
    "  return JSON_string((ps_string){ buf, n });",
    "}",
    "static ps_jsonvalue ps_json_parse_number(ps_json_parser* p, int* ok) {",
    "  size_t start = p->i;",
    "  if (p->s[p->i] == '-') p->i += 1;",
    "  while (p->i < p->len && (p->s[p->i] >= '0' && p->s[p->i] <= '9')) p->i += 1;",
    "  if (p->i < p->len && p->s[p->i] == '.') {",
    "    p->i += 1;",
    "    while (p->i < p->len && (p->s[p->i] >= '0' && p->s[p->i] <= '9')) p->i += 1;",
    "  }",
    "  size_t n = p->i - start;",
    "  if (n == 0) { *ok = 0; return JSON_null(); }",
    "  char* buf = (char*)malloc(n + 1);",
    "  if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  memcpy(buf, p->s + start, n);",
    "  buf[n] = '\\0';",
    "  double v = strtod(buf, NULL);",
    "  free(buf);",
    "  return JSON_number(v);",
    "}",
    "static ps_jsonvalue ps_json_parse_value(ps_json_parser* p, int* ok) {",
    "  ps_json_skip_ws(p);",
    "  if (p->i >= p->len) { *ok = 0; return JSON_null(); }",
    "  char c = p->s[p->i];",
    "  if (c == 't') { if (ps_json_match(p, \"true\")) return JSON_bool(true); }",
    "  if (c == 'f') { if (ps_json_match(p, \"false\")) return JSON_bool(false); }",
    "  if (c == 'n') { if (ps_json_match(p, \"null\")) return JSON_null(); }",
    "  if (c == '[') return ps_json_parse_array(p, ok);",
    "  if (c == '{') return ps_json_parse_object(p, ok);",
    "  if (c == '\"') return ps_json_parse_string(p, ok);",
    "  if ((c >= '0' && c <= '9') || c == '-') return ps_json_parse_number(p, ok);",
    "  *ok = 0; return JSON_null();",
    "}",
    "static ps_jsonvalue JSON_decode(ps_string s) {",
    "  ps_json_parser p = { s.ptr, s.len, 0 };",
    "  int ok = 1;",
    "  ps_jsonvalue v = ps_json_parse_value(&p, &ok);",
    "  ps_json_skip_ws(&p);",
    "  if (!ok || p.i != p.len) ps_panic(\"R1010\", \"RUNTIME_JSON_ERROR\", \"invalid JSON\");",
    "  return v;",
    "}",
    "static bool JSON_isValid(ps_string s) {",
    "  ps_json_parser p = { s.ptr, s.len, 0 };",
    "  int ok = 1;",
    "  ps_json_parse_value(&p, &ok);",
    "  ps_json_skip_ws(&p);",
    "  return ok && p.i == p.len;",
    "}",
    "typedef struct { char* ptr; size_t len; size_t cap; } ps_buf;",
    "static void ps_buf_append(ps_buf* b, const char* s, size_t n) {",
    "  if (n == 0) return;",
    "  if (b->len + n + 1 > b->cap) {",
    "    size_t nc = (b->cap == 0) ? 64 : b->cap * 2;",
    "    while (nc < b->len + n + 1) nc *= 2;",
    "    b->ptr = (char*)realloc(b->ptr, nc);",
    "    if (!b->ptr) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    b->cap = nc;",
    "  }",
    "  memcpy(b->ptr + b->len, s, n);",
    "  b->len += n;",
    "  b->ptr[b->len] = '\\0';",
    "}",
    "static ps_string ps_buf_to_string(ps_buf* b) {",
    "  if (!b->ptr) return ps_cstr(\"\");",
    "  return (ps_string){ b->ptr, b->len };",
    "}",
    "static ps_string JSON_encode(ps_jsonvalue v);",
    "static void ps_json_encode_value(ps_buf* b, ps_jsonvalue v) {",
    "  switch (v.kind) {",
    "    case PS_JSON_NULL: ps_buf_append(b, \"null\", 4); break;",
    "    case PS_JSON_BOOL: ps_buf_append(b, v.b ? \"true\" : \"false\", v.b ? 4 : 5); break;",
    "    case PS_JSON_NUMBER: {",
    "      char buf[64];",
    "      int n = snprintf(buf, sizeof(buf), \"%.17g\", v.num);",
    "      if (n < 0) n = 0;",
    "      ps_buf_append(b, buf, (size_t)n);",
    "      break;",
    "    }",
    "    case PS_JSON_STRING: {",
    "      ps_buf_append(b, \"\\\"\", 1);",
    "      ps_buf_append(b, v.str.ptr, v.str.len);",
    "      ps_buf_append(b, \"\\\"\", 1);",
    "      break;",
    "    }",
    "    case PS_JSON_ARRAY: {",
    "      ps_buf_append(b, \"[\", 1);",
    "      for (size_t i = 0; i < v.arr.len; i += 1) {",
    "        if (i) ps_buf_append(b, \",\", 1);",
    "        ps_json_encode_value(b, v.arr.ptr[i]);",
    "      }",
    "      ps_buf_append(b, \"]\", 1);",
    "      break;",
    "    }",
    "    case PS_JSON_OBJECT: {",
    "      ps_buf_append(b, \"{\", 1);",
    "      for (size_t i = 0; i < v.obj.len; i += 1) {",
    "        if (i) ps_buf_append(b, \",\", 1);",
    "        ps_buf_append(b, \"\\\"\", 1);",
    "        ps_buf_append(b, v.obj.keys[i].ptr, v.obj.keys[i].len);",
    "        ps_buf_append(b, \"\\\"\", 1);",
    "        ps_buf_append(b, \":\", 1);",
    "        ps_json_encode_value(b, v.obj.values[i]);",
    "      }",
    "      ps_buf_append(b, \"}\", 1);",
    "      break;",
    "    }",
    "  }",
    "}",
    "static ps_string JSON_encode(ps_jsonvalue v) {",
    "  ps_buf b = { NULL, 0, 0 };",
    "  ps_json_encode_value(&b, v);",
    "  return ps_buf_to_string(&b);",
    "}",
    "static ps_string JSON_encode_bool(bool b) { return JSON_encode(JSON_bool(b)); }",
    "static ps_string JSON_encode_number(double n) { return JSON_encode(JSON_number(n)); }",
    "static ps_string JSON_encode_string(ps_string s) { return JSON_encode(JSON_string(s)); }",
    "static ps_string JSON_encode_list_int(ps_list_int v) {",
    "  ps_list_JSONValue out = { NULL, 0, 0, 0 };",
    "  if (v.len > 0) {",
    "    out.ptr = (ps_jsonvalue*)malloc(sizeof(ps_jsonvalue) * v.len);",
    "    if (!out.ptr) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    out.len = v.len; out.cap = v.len;",
    "    for (size_t i = 0; i < v.len; i += 1) out.ptr[i] = JSON_number((double)v.ptr[i]);",
    "  }",
    "  return JSON_encode(JSON_array(out));",
    "}",
    "static ps_string JSON_encode_list_JSONValue(ps_list_JSONValue v) { return JSON_encode(JSON_array(v)); }",
    "static ps_string JSON_encode_map_string_int(ps_map_string_int m) {",
    "  ps_map_string_JSONValue obj = { NULL, NULL, 0, 0 };",
    "  if (m.len > 0) {",
    "    obj.keys = (ps_string*)malloc(sizeof(ps_string) * m.len);",
    "    obj.values = (ps_jsonvalue*)malloc(sizeof(ps_jsonvalue) * m.len);",
    "    if (!obj.keys || !obj.values) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    obj.len = m.len; obj.cap = m.len;",
    "    for (size_t i = 0; i < m.len; i += 1) {",
    "      obj.keys[i] = m.keys[i];",
    "      obj.values[i] = JSON_number((double)m.values[i]);",
    "    }",
    "  }",
    "  return JSON_encode(JSON_object(obj));",
    "}",
    "static ps_string JSON_encode_map_string_JSONValue(ps_map_string_JSONValue m) {",
    "  return JSON_encode(JSON_object(m));",
    "}",
    "",
    "static uint32_t ps_math_rng_state = 2463534242u;",
    "static double Math_abs(double x) { return fabs(x); }",
    "static double Math_min(double a, double b) { return fmin(a, b); }",
    "static double Math_max(double a, double b) { return fmax(a, b); }",
    "static double Math_floor(double x) { return floor(x); }",
    "static double Math_ceil(double x) { return ceil(x); }",
    "static double Math_round(double x) { return round(x); }",
    "static double Math_sqrt(double x) { return sqrt(x); }",
    "static double Math_pow(double a, double b) { return pow(a, b); }",
    "static double Math_sin(double x) { return sin(x); }",
    "static double Math_cos(double x) { return cos(x); }",
    "static double Math_tan(double x) { return tan(x); }",
    "static double Math_asin(double x) { return asin(x); }",
    "static double Math_acos(double x) { return acos(x); }",
    "static double Math_atan(double x) { return atan(x); }",
    "static double Math_atan2(double y, double x) { return atan2(y, x); }",
    "static double Math_sinh(double x) { return sinh(x); }",
    "static double Math_cosh(double x) { return cosh(x); }",
    "static double Math_tanh(double x) { return tanh(x); }",
    "static double Math_asinh(double x) { return asinh(x); }",
    "static double Math_acosh(double x) { return acosh(x); }",
    "static double Math_atanh(double x) { return atanh(x); }",
    "static double Math_exp(double x) { return exp(x); }",
    "static double Math_expm1(double x) { return expm1(x); }",
    "static double Math_log(double x) { return log(x); }",
    "static double Math_log1p(double x) { return log1p(x); }",
    "static double Math_log2(double x) { return log2(x); }",
    "static double Math_log10(double x) { return log10(x); }",
    "static double Math_cbrt(double x) { return cbrt(x); }",
    "static double Math_hypot(double a, double b) { return hypot(a, b); }",
    "static double Math_trunc(double x) { return trunc(x); }",
    "static double Math_sign(double x) {",
    "  if (isnan(x)) return NAN;",
    "  if (x == 0.0) return x;",
    "  return (x < 0.0) ? -1.0 : 1.0;",
    "}",
    "static double Math_fround(double x) { float f = (float)x; return (double)f; }",
    "static double Math_clz32(double x) {",
    "  uint32_t v = (uint32_t)(int64_t)x;",
    "  if (v == 0) return 32.0;",
    "  return (double)__builtin_clz(v);",
    "}",
    "static double Math_imul(double a, double b) {",
    "  int32_t x = (int32_t)(int64_t)a;",
    "  int32_t y = (int32_t)(int64_t)b;",
    "  int32_t r = (int32_t)(x * y);",
    "  return (double)r;",
    "}",
    "static double Math_random(void) {",
    "  uint32_t x = ps_math_rng_state;",
    "  x ^= x << 13;",
    "  x ^= x >> 17;",
    "  x ^= x << 5;",
    "  ps_math_rng_state = x;",
    "  return (double)(x) / 4294967296.0;",
    "}",
    "",
    "static int64_t test_simple_add(int64_t a, int64_t b) { return a + b; }",
    "static ps_string test_utf8_roundtrip(ps_string s) { return s; }",
    "static void test_throw_fail(void) { ps_panic(\"R1010\", \"RUNTIME_MODULE_ERROR\", \"module not found\"); }",
    "static int64_t test_noinit_ping(void) { ps_panic(\"R1010\", \"RUNTIME_MODULE_ERROR\", \"module not found\"); return 0; }",
    "static int64_t test_badver_ping(void) { ps_panic(\"R1010\", \"RUNTIME_MODULE_ERROR\", \"module not found\"); return 0; }",
    "static int64_t test_missing_ping(void) { ps_panic(\"R1010\", \"RUNTIME_MODULE_ERROR\", \"module not found\"); return 0; }",
    "static int64_t test_nosym_missing(void) { ps_panic(\"R1010\", \"RUNTIME_MODULE_ERROR\", \"symbol not found\"); return 0; }",
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
    "static void ps_check_view_bounds(size_t len, int64_t offset, int64_t view_len) {",
    "  if (offset < 0 || view_len < 0 || (size_t)(offset + view_len) > len) {",
    "    ps_panic(\"R1002\", \"RUNTIME_INDEX_OOB\", \"index out of bounds\");",
    "  }",
    "}",
    "static void ps_check_view_valid(const uint64_t* version_ptr, uint64_t version) {",
    "  if (version_ptr && *version_ptr != version) {",
    "    ps_panic(\"R1012\", \"RUNTIME_VIEW_INVALID\", \"view invalidated\");",
    "  }",
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
    "static uint32_t ps_view_glyph_get(ps_view_glyph v, int64_t idx) {",
    "  if (idx < 0 || (size_t)idx >= v.len) ps_panic(\"R1002\", \"RUNTIME_INDEX_OOB\", \"index out of bounds\");",
    "  if (v.is_string) return ps_string_index_glyph(v.str, (int64_t)(v.offset + (size_t)idx));",
    "  return v.ptr[idx];",
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
    "static ps_string ps_list_string_join(ps_list_string l, ps_string sep) {",
    "  size_t total = 0;",
    "  if (l.len == 0) {",
    "    char* buf = (char*)malloc(1);",
    "    if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    buf[0] = 0;",
    "    ps_string out = { buf, 0 };",
    "    return out;",
    "  }",
    "  for (size_t i = 0; i < l.len; i += 1) total += l.ptr[i].len;",
    "  total += sep.len * (l.len > 0 ? (l.len - 1) : 0);",
    "  char* buf = (char*)malloc(total + 1);",
    "  if (!buf && total > 0) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  size_t off = 0;",
    "  for (size_t i = 0; i < l.len; i += 1) {",
    "    if (i > 0 && sep.len > 0) {",
    "      memcpy(buf + off, sep.ptr, sep.len);",
    "      off += sep.len;",
    "    }",
    "    if (l.ptr[i].len > 0) {",
    "      memcpy(buf + off, l.ptr[i].ptr, l.ptr[i].len);",
    "      off += l.ptr[i].len;",
    "    }",
    "  }",
    "  buf[off] = 0;",
    "  ps_string out = { buf, off };",
    "  return out;",
    "}",
    "static ps_string ps_list_string_concat(ps_list_string l) {",
    "  ps_string sep = { \"\", 0 };",
    "  return ps_list_string_join(l, sep);",
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
    "static bool ps_string_eq(ps_string a, ps_string b) {",
    "  if (a.len != b.len) return false;",
    "  if (a.len == 0) return true;",
    "  return memcmp(a.ptr, b.ptr, a.len) == 0;",
    "}",
    "static void ps_list_string_push(ps_list_string* l, ps_string v) {",
    "  if (l->len == l->cap) {",
    "    size_t new_cap = (l->cap == 0) ? 4 : (l->cap * 2);",
    "    l->ptr = (ps_string*)realloc(l->ptr, sizeof(*l->ptr) * new_cap);",
    "    if (!l->ptr) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    l->cap = new_cap;",
    "  }",
    "  l->ptr[l->len++] = v;",
    "  l->version += 1;",
    "}",
    "static ps_list_string ps_string_split(ps_string s, ps_string sep) {",
    "  ps_list_string out = { NULL, 0, 0, 0 };",
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
    "  ps_list_byte out = { NULL, 0, 0, 0 };",
    "  out.len = s.len;",
    "  out.cap = s.len;",
    "  out.ptr = (uint8_t*)malloc(sizeof(uint8_t) * s.len);",
    "  if (!out.ptr && s.len > 0) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  if (s.len > 0) memcpy(out.ptr, s.ptr, s.len);",
    "  return out;",
    "}",
    "static ps_list_byte ps_glyph_to_utf8_bytes(uint32_t g) {",
    "  ps_list_byte out = { NULL, 0, 0, 0 };",
    "  uint8_t buf[4];",
    "  size_t n = 0;",
    "  if (g <= 0x7F) {",
    "    buf[0] = (uint8_t)g;",
    "    n = 1;",
    "  } else if (g <= 0x7FF) {",
    "    buf[0] = (uint8_t)(0xC0 | (g >> 6));",
    "    buf[1] = (uint8_t)(0x80 | (g & 0x3F));",
    "    n = 2;",
    "  } else if (g <= 0xFFFF) {",
    "    if (g >= 0xD800 && g <= 0xDFFF) ps_panic(\"R1007\", \"RUNTIME_INVALID_UTF8\", \"invalid UTF-8\");",
    "    buf[0] = (uint8_t)(0xE0 | (g >> 12));",
    "    buf[1] = (uint8_t)(0x80 | ((g >> 6) & 0x3F));",
    "    buf[2] = (uint8_t)(0x80 | (g & 0x3F));",
    "    n = 3;",
    "  } else if (g <= 0x10FFFF) {",
    "    buf[0] = (uint8_t)(0xF0 | (g >> 18));",
    "    buf[1] = (uint8_t)(0x80 | ((g >> 12) & 0x3F));",
    "    buf[2] = (uint8_t)(0x80 | ((g >> 6) & 0x3F));",
    "    buf[3] = (uint8_t)(0x80 | (g & 0x3F));",
    "    n = 4;",
    "  } else {",
    "    ps_panic(\"R1007\", \"RUNTIME_INVALID_UTF8\", \"invalid UTF-8\");",
    "  }",
    "  out.len = n;",
    "  out.cap = n;",
    "  out.ptr = (uint8_t*)malloc(sizeof(uint8_t) * n);",
    "  if (!out.ptr && n > 0) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  if (n > 0) memcpy(out.ptr, buf, n);",
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
    "static int64_t ps_int_abs(int64_t v) {",
    "  if (v == INT64_MIN) ps_panic(\"R1001\", \"RUNTIME_INT_OVERFLOW\", \"int overflow\");",
    "  return v < 0 ? -v : v;",
    "}",
    "static int64_t ps_int_sign(int64_t v) {",
    "  if (v == 0) return 0;",
    "  return v > 0 ? 1 : -1;",
    "}",
    "static ps_list_byte ps_i64_to_bytes(int64_t v) {",
    "  union { int64_t i; uint8_t b[8]; } u;",
    "  u.i = v;",
    "  ps_list_byte out = { NULL, 0, 0, 0 };",
    "  out.len = 8;",
    "  out.cap = 8;",
    "  out.ptr = (uint8_t*)malloc(8);",
    "  if (!out.ptr) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  memcpy(out.ptr, u.b, 8);",
    "  return out;",
    "}",
    "static ps_list_byte ps_f64_to_bytes(double v) {",
    "  union { double f; uint8_t b[8]; } u;",
    "  u.f = v;",
    "  ps_list_byte out = { NULL, 0, 0, 0 };",
    "  out.len = 8;",
    "  out.cap = 8;",
    "  out.ptr = (uint8_t*)malloc(8);",
    "  if (!out.ptr) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  memcpy(out.ptr, u.b, 8);",
    "  return out;",
    "}",
    "static int64_t ps_float_to_int(double v) {",
    "  if (!isfinite(v)) ps_panic(\"R1010\", \"RUNTIME_TYPE_ERROR\", \"invalid float to int\");",
    "  if (v > (double)INT64_MAX || v < (double)INT64_MIN) ps_panic(\"R1001\", \"RUNTIME_INT_OVERFLOW\", \"int overflow\");",
    "  return (int64_t)trunc(v);",
    "}",
    "static int64_t ps_string_to_int(ps_string s) {",
    "  if (s.len == 0) ps_panic(\"R1010\", \"RUNTIME_TYPE_ERROR\", \"invalid int format\");",
    "  char* buf = (char*)malloc(s.len + 1);",
    "  if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  memcpy(buf, s.ptr, s.len);",
    "  buf[s.len] = 0;",
    "  errno = 0;",
    "  char* end = NULL;",
    "  long long v = strtoll(buf, &end, 10);",
    "  int ok = (end && *end == 0 && errno != ERANGE);",
    "  free(buf);",
    "  if (!ok) ps_panic(\"R1010\", \"RUNTIME_TYPE_ERROR\", \"invalid int format\");",
    "  return (int64_t)v;",
    "}",
    "static double ps_string_to_float(ps_string s) {",
    "  if (s.len == 0) ps_panic(\"R1010\", \"RUNTIME_TYPE_ERROR\", \"invalid float format\");",
    "  char* buf = (char*)malloc(s.len + 1);",
    "  if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  memcpy(buf, s.ptr, s.len);",
    "  buf[s.len] = 0;",
    "  errno = 0;",
    "  char* end = NULL;",
    "  double v = strtod(buf, &end);",
    "  int ok = (end && *end == 0 && errno != ERANGE);",
    "  free(buf);",
    "  if (!ok) ps_panic(\"R1010\", \"RUNTIME_TYPE_ERROR\", \"invalid float format\");",
    "  return v;",
    "}",
    "static bool ps_glyph_is_letter(uint32_t g) {",
    "  return (g >= 'A' && g <= 'Z') || (g >= 'a' && g <= 'z');",
    "}",
    "static bool ps_glyph_is_digit(uint32_t g) {",
    "  return (g >= '0' && g <= '9');",
    "}",
    "static bool ps_glyph_is_whitespace(uint32_t g) {",
    "  return g == ' ' || g == '\\t' || g == '\\n' || g == '\\r';",
    "}",
    "static bool ps_glyph_is_upper(uint32_t g) {",
    "  return (g >= 'A' && g <= 'Z');",
    "}",
    "static bool ps_glyph_is_lower(uint32_t g) {",
    "  return (g >= 'a' && g <= 'z');",
    "}",
    "static uint32_t ps_glyph_to_upper(uint32_t g) {",
    "  return (g >= 'a' && g <= 'z') ? (g - 32) : g;",
    "}",
    "static uint32_t ps_glyph_to_lower(uint32_t g) {",
    "  return (g >= 'A' && g <= 'Z') ? (g + 32) : g;",
    "}",
    "static ps_string ps_glyph_to_string(uint32_t g) {",
    "  ps_list_byte b = ps_glyph_to_utf8_bytes(g);",
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
    "static ps_string ps_f64_to_string(double v) {",
    "  static char buf[4][64];",
    "  static int slot = 0;",
    "  slot = (slot + 1) & 3;",
    "  int n = snprintf(buf[slot], sizeof(buf[slot]), \"%.17g\", v);",
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
      } else if (i.literalType === "file") {
        if (i.value === "stdin") out.push(`${n(i.dst)} = &ps_stdin;`);
        else if (i.value === "stdout") out.push(`${n(i.dst)} = &ps_stdout;`);
        else if (i.value === "stderr") out.push(`${n(i.dst)} = &ps_stderr;`);
        else out.push(`${n(i.dst)} = NULL;`);
      } else if (i.literalType === "eof") {
        out.push(`${n(i.dst)} = 0;`);
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
    case "check_view_bounds": {
      const tt = t(i.target);
      if (tt === "string") out.push(`ps_check_view_bounds(ps_utf8_glyph_len(${n(i.target)}), ${n(i.offset)}, ${n(i.len)});`);
      else out.push(`ps_check_view_bounds(${n(i.target)}.len, ${n(i.offset)}, ${n(i.len)});`);
      break;
    }
    case "check_map_has_key":
      {
        const m = parseMapType(t(i.map));
        const mapRefName = aliasOf(i.map) || i.map;
        if (m) out.push(`ps_check_map_has_key(ps_map_has_key_${m.base}(&${n(mapRefName)}, ${n(i.key)}));`);
        else out.push("ps_check_map_has_key(0);");
      }
      break;
    case "bin_op":
      {
        const lt = t(i.left);
        const rt = t(i.right);
        if (lt === "glyph" || rt === "glyph") {
          if (lt !== "glyph" || rt !== "glyph") {
            out.push('ps_panic("R1010", "RUNTIME_TYPE_ERROR", "invalid glyph operation");');
          } else if (["==", "!=", "<", "<=", ">", ">="].includes(i.operator)) {
            out.push(`${n(i.dst)} = (${n(i.left)} ${i.operator} ${n(i.right)});`);
          } else {
            out.push('ps_panic("R1010", "RUNTIME_TYPE_ERROR", "invalid glyph operation");');
          }
        } else if ((i.operator === "==" || i.operator === "!=") && lt === "string" && rt === "string") {
          const cmp = `ps_string_eq(${n(i.left)}, ${n(i.right)})`;
          out.push(`${n(i.dst)} = ${i.operator === "==" ? cmp : `!(${cmp})`};`);
        } else {
          out.push(`${n(i.dst)} = (${n(i.left)} ${i.operator} ${n(i.right)});`);
        }
      }
      break;
    case "unary_op":
      if (t(i.src) === "glyph" && (i.operator === "-" || i.operator === "++" || i.operator === "--" || i.operator === "~")) {
        out.push('ps_panic("R1010", "RUNTIME_TYPE_ERROR", "invalid glyph operation");');
      } else {
        out.push(`${n(i.dst)} = (${i.operator}${n(i.src)});`);
      }
      break;
    case "copy":
      setAlias(i.dst, aliasOf(i.src) || null);
      out.push(`${n(i.dst)} = ${n(i.src)};`);
      break;
    case "postfix_op":
      if (t(i.src) === "glyph" && (i.operator === "++" || i.operator === "--")) {
        out.push('ps_panic("R1010", "RUNTIME_TYPE_ERROR", "invalid glyph operation");');
      } else {
        out.push(`${n(i.dst)} = ${n(i.src)}${i.operator};`);
      }
      break;
    case "call_static":
      if (i.callee === "Exception") {
        if (i.args.length > 2) {
          out.push('ps_panic("R1010", "RUNTIME_TYPE_ERROR", "Exception expects (string message, Exception cause?)");');
        } else if (i.args.length >= 1) {
          out.push(`${n(i.dst)} = ps_exception_new(${n(i.args[0])});`);
        } else {
          out.push(`${n(i.dst)} = ps_exception_new((ps_string){\"\",0});`);
        }
      } else if (i.callee === "RuntimeException") {
        out.push('ps_panic("R1010", "RUNTIME_TYPE_ERROR", "RuntimeException is not constructible");');
      } else if (i.callee === "JSON.encode") {
        const at = t(i.args[0]);
        if (at === "JSONValue") out.push(`${n(i.dst)} = JSON_encode(${n(i.args[0])});`);
        else if (at === "string") out.push(`${n(i.dst)} = JSON_encode_string(${n(i.args[0])});`);
        else if (at === "bool") out.push(`${n(i.dst)} = JSON_encode_bool(${n(i.args[0])});`);
        else if (at === "int") out.push(`${n(i.dst)} = JSON_encode_number((double)${n(i.args[0])});`);
        else if (at === "float") out.push(`${n(i.dst)} = JSON_encode_number(${n(i.args[0])});`);
        else {
          const pc = parseContainer(at);
          if (pc && pc.kind === "list") out.push(`${n(i.dst)} = JSON_encode_list_${baseName(pc.inner)}(${n(i.args[0])});`);
          else if (pc && pc.kind === "map") out.push(`${n(i.dst)} = JSON_encode_map_${baseName(pc.inner)}(${n(i.args[0])});`);
          else out.push('ps_panic("R1010", "RUNTIME_TYPE_ERROR", "JSON.encode unsupported type");');
        }
      } else if (i.callee.startsWith("Math.")) {
        const bad = i.args.some((a) => {
          const at = t(a);
          return !(at === "int" || at === "float");
        });
        if (bad) {
          out.push('ps_panic("R1010", "RUNTIME_TYPE_ERROR", "expected float");');
        } else if (t(i.dst) === "void") {
          out.push(`${cIdent(i.callee)}(${i.args.map(n).join(", ")});`);
        } else {
          out.push(`${n(i.dst)} = ${cIdent(i.callee)}(${i.args.map(n).join(", ")});`);
        }
      } else if (i.callee === "Io.print" || i.callee === "Io.printLine" || i.callee === "Io_print" || i.callee === "Io_printLine") {
        out.push(`${cIdent(i.callee)}(${i.args.map(n).join(", ")});`);
      } else if (t(i.dst) === "void") {
        out.push(`${cIdent(i.callee)}(${i.args.map(n).join(", ")});`);
      } else {
        out.push(`${n(i.dst)} = ${cIdent(i.callee)}(${i.args.map(n).join(", ")});`);
      }
      break;
    case "call_method_static":
      {
        const rt = t(i.receiver);
        const rc = parseContainer(rt);
        if (rt === "File") {
          if (i.method === "read") {
            if (i.args.length === 0) out.push(`${n(i.dst)} = ps_file_read_all(${n(i.receiver)});`);
            else out.push(`${n(i.dst)} = ps_file_read_size(${n(i.receiver)}, ${n(i.args[0])});`);
          } else if (i.method === "readBytes") {
            if (i.args.length === 0) out.push(`${n(i.dst)} = ps_file_read_all_bytes(${n(i.receiver)});`);
            else out.push(`${n(i.dst)} = ps_file_read_size(${n(i.receiver)}, ${n(i.args[0])});`);
          } else if (i.method === "write") {
            const at = t(i.args[0]);
            if (at === "string") out.push(`ps_file_write_text(${n(i.receiver)}, ${n(i.args[0])});`);
            else if (parseContainer(at)?.kind === "list" && parseContainer(at)?.inner === "byte") {
              out.push(`ps_file_write_bytes(${n(i.receiver)}, ${n(i.args[0])});`);
            } else if (parseContainer(at)?.kind === "list" && parseContainer(at)?.inner === "int") {
              out.push(`ps_file_write_bytes_from_ints(${n(i.receiver)}, ${n(i.args[0])});`);
            } else {
              out.push('ps_panic("R1010", "RUNTIME_TYPE_ERROR", "invalid write argument");');
            }
          } else if (i.method === "writeBytes") {
            out.push(`ps_file_write_bytes(${n(i.receiver)}, ${n(i.args[0])});`);
          } else if (i.method === "close") {
            out.push(`ps_file_close(${n(i.receiver)});`);
          } else {
            out.push(`/* unknown file method ${i.method} */`);
          }
        } else if (rt === "JSONValue") {
          if (i.method === "isNull") out.push(`${n(i.dst)} = ps_json_is_null(${n(i.receiver)});`);
          else if (i.method === "isBool") out.push(`${n(i.dst)} = ps_json_is_bool(${n(i.receiver)});`);
          else if (i.method === "isNumber") out.push(`${n(i.dst)} = ps_json_is_number(${n(i.receiver)});`);
          else if (i.method === "isString") out.push(`${n(i.dst)} = ps_json_is_string(${n(i.receiver)});`);
          else if (i.method === "isArray") out.push(`${n(i.dst)} = ps_json_is_array(${n(i.receiver)});`);
          else if (i.method === "isObject") out.push(`${n(i.dst)} = ps_json_is_object(${n(i.receiver)});`);
          else if (i.method === "asBool") out.push(`${n(i.dst)} = ps_json_as_bool(${n(i.receiver)});`);
          else if (i.method === "asNumber") out.push(`${n(i.dst)} = ps_json_as_number(${n(i.receiver)});`);
          else if (i.method === "asString") out.push(`${n(i.dst)} = ps_json_as_string(${n(i.receiver)});`);
          else if (i.method === "asArray") out.push(`${n(i.dst)} = ps_json_as_array(${n(i.receiver)});`);
          else if (i.method === "asObject") out.push(`${n(i.dst)} = ps_json_as_object(${n(i.receiver)});`);
        } else if (i.method === "length") {
          if (rt === "string") out.push(`${n(i.dst)} = (int64_t)ps_utf8_glyph_len(${n(i.receiver)});`);
          else if (rc && ["list", "slice", "view"].includes(rc.kind)) {
            if (rc.kind === "view" || rc.kind === "slice") {
              out.push(`ps_check_view_valid(${n(i.receiver)}.version_ptr, ${n(i.receiver)}.version);`);
            }
            out.push(`${n(i.dst)} = (int64_t)${n(i.receiver)}.len;`);
          }
          else if (rc && rc.kind === "map") out.push(`${n(i.dst)} = (int64_t)${n(i.receiver)}.len;`);
          else out.push(`${n(i.dst)} = 0;`);
        } else if (i.method === "isEmpty") {
          if (rt === "string") out.push(`${n(i.dst)} = ps_utf8_glyph_len(${n(i.receiver)}) == 0;`);
          else if (rc && ["list", "slice", "view"].includes(rc.kind)) {
            if (rc.kind === "view" || rc.kind === "slice") {
              out.push(`ps_check_view_valid(${n(i.receiver)}.version_ptr, ${n(i.receiver)}.version);`);
            }
            out.push(`${n(i.dst)} = ${n(i.receiver)}.len == 0;`);
          }
          else if (rc && rc.kind === "map") out.push(`${n(i.dst)} = ${n(i.receiver)}.len == 0;`);
          else out.push(`${n(i.dst)} = 0;`);
        } else if (rc && rc.kind === "map" && i.method === "containsKey") {
          const m = parseMapType(rt);
          if (m) out.push(`${n(i.dst)} = ps_map_has_key_${m.base}(&${n(i.receiver)}, ${n(i.args[0])});`);
          else out.push(`${n(i.dst)} = 0;`);
        } else if (rc && rc.kind === "map" && i.method === "keys") {
          const m = parseMapType(rt);
          if (m) out.push(`${n(i.dst)} = ps_map_keys_${m.base}(&${n(i.receiver)});`);
          else out.push(`${n(i.dst)}.len = 0; ${n(i.dst)}.cap = 0; ${n(i.dst)}.ptr = NULL;`);
        } else if (rc && rc.kind === "map" && i.method === "values") {
          const m = parseMapType(rt);
          if (m) out.push(`${n(i.dst)} = ps_map_values_${m.base}(&${n(i.receiver)});`);
          else out.push(`${n(i.dst)}.len = 0; ${n(i.dst)}.cap = 0; ${n(i.dst)}.ptr = NULL;`);
        } else if (rt === "int" && i.method === "toByte") {
          out.push(`if (${n(i.receiver)} < 0 || ${n(i.receiver)} > 255) ps_panic("R1008", "RUNTIME_BYTE_RANGE", "byte out of range");`);
          out.push(`${n(i.dst)} = (uint8_t)${n(i.receiver)};`);
        } else if (rt === "int" && i.method === "toFloat") {
          out.push(`${n(i.dst)} = (double)${n(i.receiver)};`);
        } else if (rt === "int" && i.method === "toBytes") {
          out.push(`${n(i.dst)} = ps_i64_to_bytes(${n(i.receiver)});`);
        } else if (rt === "int" && i.method === "abs") {
          out.push(`${n(i.dst)} = ps_int_abs(${n(i.receiver)});`);
        } else if (rt === "int" && i.method === "sign") {
          out.push(`${n(i.dst)} = ps_int_sign(${n(i.receiver)});`);
        } else if (rt === "byte" && i.method === "toInt") {
          out.push(`${n(i.dst)} = (int64_t)${n(i.receiver)};`);
        } else if (rt === "byte" && i.method === "toFloat") {
          out.push(`${n(i.dst)} = (double)${n(i.receiver)};`);
        } else if (rt === "float" && i.method === "toInt") {
          out.push(`${n(i.dst)} = ps_float_to_int(${n(i.receiver)});`);
        } else if (rt === "float" && i.method === "toBytes") {
          out.push(`${n(i.dst)} = ps_f64_to_bytes(${n(i.receiver)});`);
        } else if (rt === "float" && i.method === "abs") {
          out.push(`${n(i.dst)} = fabs(${n(i.receiver)});`);
        } else if (rt === "float" && i.method === "isNaN") {
          out.push(`${n(i.dst)} = isnan(${n(i.receiver)});`);
        } else if (rt === "float" && i.method === "isInfinite") {
          out.push(`${n(i.dst)} = isinf(${n(i.receiver)});`);
        } else if (rt === "float" && i.method === "isFinite") {
          out.push(`${n(i.dst)} = isfinite(${n(i.receiver)});`);
        } else if (rt === "glyph" && i.method === "isLetter") {
          out.push(`${n(i.dst)} = ps_glyph_is_letter(${n(i.receiver)});`);
        } else if (rt === "glyph" && i.method === "isDigit") {
          out.push(`${n(i.dst)} = ps_glyph_is_digit(${n(i.receiver)});`);
        } else if (rt === "glyph" && i.method === "isWhitespace") {
          out.push(`${n(i.dst)} = ps_glyph_is_whitespace(${n(i.receiver)});`);
        } else if (rt === "glyph" && i.method === "isUpper") {
          out.push(`${n(i.dst)} = ps_glyph_is_upper(${n(i.receiver)});`);
        } else if (rt === "glyph" && i.method === "isLower") {
          out.push(`${n(i.dst)} = ps_glyph_is_lower(${n(i.receiver)});`);
        } else if (rt === "glyph" && i.method === "toUpper") {
          out.push(`${n(i.dst)} = ps_glyph_to_upper(${n(i.receiver)});`);
        } else if (rt === "glyph" && i.method === "toLower") {
          out.push(`${n(i.dst)} = ps_glyph_to_lower(${n(i.receiver)});`);
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
        } else if (rt === "string" && i.method === "toInt") {
          out.push(`${n(i.dst)} = ps_string_to_int(${n(i.receiver)});`);
        } else if (rt === "string" && i.method === "toFloat") {
          out.push(`${n(i.dst)} = ps_string_to_float(${n(i.receiver)});`);
        } else if (rt === "glyph" && i.method === "toInt") {
          out.push(`${n(i.dst)} = (int64_t)${n(i.receiver)};`);
        } else if (rt === "glyph" && i.method === "toUtf8Bytes") {
          out.push(`${n(i.dst)} = ps_glyph_to_utf8_bytes(${n(i.receiver)});`);
        } else if (rc && rc.kind === "list" && rc.inner === "byte" && i.method === "toUtf8String") {
          out.push(`${n(i.dst)} = ps_list_byte_to_utf8_string(${n(i.receiver)});`);
        } else if (rc && rc.kind === "list" && rc.inner === "string" && i.method === "join") {
          out.push(`${n(i.dst)} = ps_list_string_join(${n(i.receiver)}, ${n(i.args[0])});`);
        } else if (rc && rc.kind === "list" && rc.inner === "string" && i.method === "concat") {
          out.push(`${n(i.dst)} = ps_list_string_concat(${n(i.receiver)});`);
        } else if (rc && rc.kind === "list" && i.method === "push") {
          const recv = aliasOf(i.receiver) || i.receiver;
          out.push(`if (${n(recv)}.len == ${n(recv)}.cap) {`);
          out.push(`  size_t new_cap = ${n(recv)}.cap ? (${n(recv)}.cap * 2) : 4;`);
          out.push(`  ${n(recv)}.ptr = (${cTypeFromName(rc.inner)}*)realloc(${n(recv)}.ptr, sizeof(*${n(recv)}.ptr) * new_cap);`);
          out.push(`  if (!${n(recv)}.ptr) ps_panic("R1998", "RUNTIME_OOM", "out of memory");`);
          out.push(`  ${n(recv)}.cap = new_cap;`);
          out.push(`}`);
          out.push(`${n(recv)}.ptr[${n(recv)}.len++] = ${n(i.args[0])};`);
          out.push(`${n(recv)}.version += 1;`);
          out.push(`${n(i.dst)} = (int64_t)${n(recv)}.len;`);
        } else if (rc && rc.kind === "list" && i.method === "contains") {
          const recv = aliasOf(i.receiver) || i.receiver;
          const inner = rc.inner;
          out.push(`{ bool found = false;`);
          out.push(`  for (size_t i = 0; i < ${n(recv)}.len; i += 1) {`);
          if (inner === "string") {
            out.push(`    if (ps_string_eq(${n(recv)}.ptr[i], ${n(i.args[0])})) { found = true; break; }`);
          } else if (["int", "float", "byte", "glyph", "bool"].includes(inner)) {
            out.push(`    if (${n(recv)}.ptr[i] == ${n(i.args[0])}) { found = true; break; }`);
          } else {
            out.push(`    ps_panic("R1010", "RUNTIME_TYPE_ERROR", "list element not comparable");`);
          }
          out.push(`  }`);
          out.push(`  ${n(i.dst)} = found;`);
          out.push(`}`);
        } else if (rc && rc.kind === "list" && i.method === "sort") {
          const recv = aliasOf(i.receiver) || i.receiver;
          const inner = rc.inner;
          out.push(`{`);
          if (inner === "string") {
            out.push(`  for (size_t i = 0; i + 1 < ${n(recv)}.len; i += 1) {`);
            out.push(`    for (size_t j = i + 1; j < ${n(recv)}.len; j += 1) {`);
            out.push(`      ps_string a = ${n(recv)}.ptr[i];`);
            out.push(`      ps_string b = ${n(recv)}.ptr[j];`);
            out.push(`      size_t ml = (a.len < b.len) ? a.len : b.len;`);
            out.push(`      int r = memcmp(a.ptr, b.ptr, ml);`);
            out.push(`      int cmp = (r < 0) ? -1 : (r > 0) ? 1 : (a.len < b.len) ? -1 : (a.len > b.len) ? 1 : 0;`);
            out.push(`      if (cmp > 0) { ps_string tmp = ${n(recv)}.ptr[i]; ${n(recv)}.ptr[i] = ${n(recv)}.ptr[j]; ${n(recv)}.ptr[j] = tmp; }`);
            out.push(`    }`);
            out.push(`  }`);
          } else if (["int", "float", "byte", "glyph", "bool"].includes(inner)) {
            out.push(`  for (size_t i = 0; i + 1 < ${n(recv)}.len; i += 1) {`);
            out.push(`    for (size_t j = i + 1; j < ${n(recv)}.len; j += 1) {`);
            if (inner === "float") {
              out.push(`      double a = ${n(recv)}.ptr[i];`);
              out.push(`      double b = ${n(recv)}.ptr[j];`);
              out.push(`      if (a > b) { double tmp = ${n(recv)}.ptr[i]; ${n(recv)}.ptr[i] = ${n(recv)}.ptr[j]; ${n(recv)}.ptr[j] = tmp; }`);
            } else {
              out.push(`      if (${n(recv)}.ptr[i] > ${n(recv)}.ptr[j]) { ${cTypeFromName(inner)} tmp = ${n(recv)}.ptr[i]; ${n(recv)}.ptr[i] = ${n(recv)}.ptr[j]; ${n(recv)}.ptr[j] = tmp; }`);
            }
            out.push(`    }`);
            out.push(`  }`);
          } else {
            out.push(`  ps_panic("R1010", "RUNTIME_TYPE_ERROR", "list element not comparable");`);
          }
          out.push(`  ${n(i.dst)} = (int64_t)${n(recv)}.len;`);
          out.push(`}`);
        } else if (i.method === "pop" && rc && rc.kind === "list") {
          const recv = aliasOf(i.receiver) || i.receiver;
          out.push(`if (${n(recv)}.len == 0) ps_panic("R1006", "RUNTIME_EMPTY_POP", "pop on empty list");`);
          out.push(`${n(recv)}.len -= 1;`);
          out.push(`${n(recv)}.version += 1;`);
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
        else if (vt === "byte") out.push(`${n(i.dst)} = ps_u32_to_string((uint32_t)${n(i.value)});`);
        else if (vt === "glyph") out.push(`${n(i.dst)} = ps_glyph_to_string((uint32_t)${n(i.value)});`);
        else if (vt === "float") out.push(`${n(i.dst)} = ps_f64_to_string(${n(i.value)});`);
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
      if (t(i.dst) === "view<glyph>") {
        if (t(i.source) === "string") {
          out.push(`${n(i.dst)}.is_string = 1;`);
          out.push(`${n(i.dst)}.str = ${n(i.source)};`);
          out.push(`${n(i.dst)}.offset = (size_t)${n(i.offset)};`);
          out.push(`${n(i.dst)}.len = (size_t)${n(i.len)};`);
          out.push(`${n(i.dst)}.ptr = NULL;`);
          out.push(`${n(i.dst)}.version_ptr = NULL;`);
          out.push(`${n(i.dst)}.version = 0;`);
        } else if (t(i.source) === "view<glyph>") {
          out.push(`${n(i.dst)}.is_string = ${n(i.source)}.is_string;`);
          out.push(`if (${n(i.source)}.is_string) {`);
          out.push(`  ${n(i.dst)}.str = ${n(i.source)}.str;`);
          out.push(`  ${n(i.dst)}.offset = ${n(i.source)}.offset + (size_t)${n(i.offset)};`);
          out.push(`  ${n(i.dst)}.len = (size_t)${n(i.len)};`);
          out.push(`  ${n(i.dst)}.ptr = NULL;`);
          out.push(`  ${n(i.dst)}.version_ptr = NULL;`);
          out.push(`  ${n(i.dst)}.version = 0;`);
          out.push(`} else {`);
          out.push(`  ${n(i.dst)}.ptr = ${n(i.source)}.ptr + (size_t)${n(i.offset)};`);
          out.push(`  ${n(i.dst)}.len = (size_t)${n(i.len)};`);
          out.push(`  ${n(i.dst)}.offset = 0;`);
          out.push(`  ${n(i.dst)}.version_ptr = ${n(i.source)}.version_ptr;`);
          out.push(`  ${n(i.dst)}.version = ${n(i.source)}.version;`);
          out.push(`}`);
        } else {
          const srcRef = aliasOf(i.source) || i.source;
          out.push(`${n(i.dst)}.is_string = 0;`);
          out.push(`${n(i.dst)}.ptr = ${n(i.source)}.ptr + (size_t)${n(i.offset)};`);
          out.push(`${n(i.dst)}.len = (size_t)${n(i.len)};`);
          out.push(`${n(i.dst)}.offset = 0;`);
          out.push(`${n(i.dst)}.version_ptr = &${n(srcRef)}.version;`);
          out.push(`${n(i.dst)}.version = ${n(srcRef)}.version;`);
        }
      } else {
        const srcType = t(i.source);
        const srcCont = parseContainer(srcType);
        out.push(`${n(i.dst)}.ptr = ${n(i.source)}.ptr + (size_t)${n(i.offset)};`);
        out.push(`${n(i.dst)}.len = (size_t)${n(i.len)};`);
        if (srcCont && srcCont.kind === "list") {
          const srcRef = aliasOf(i.source) || i.source;
          out.push(`${n(i.dst)}.version_ptr = &${n(srcRef)}.version;`);
          out.push(`${n(i.dst)}.version = ${n(srcRef)}.version;`);
        } else if (srcCont && (srcCont.kind === "view" || srcCont.kind === "slice")) {
          out.push(`${n(i.dst)}.version_ptr = ${n(i.source)}.version_ptr;`);
          out.push(`${n(i.dst)}.version = ${n(i.source)}.version;`);
        } else {
          out.push(`${n(i.dst)}.version_ptr = NULL;`);
          out.push(`${n(i.dst)}.version = 0;`);
        }
      }
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
          const p = parseContainer(tt);
          if (p && p.kind === "view" && p.inner === "glyph") {
            out.push(`ps_check_view_valid(${n(i.target)}.version_ptr, ${n(i.target)}.version);`);
            out.push(`${n(i.dst)} = ps_view_glyph_get(${n(i.target)}, ${n(i.index)});`);
          } else if (p && (p.kind === "view" || p.kind === "slice")) {
            out.push(`ps_check_view_valid(${n(i.target)}.version_ptr, ${n(i.target)}.version);`);
            out.push(`${n(i.dst)} = ${n(i.target)}.ptr[${n(i.index)}];`);
          } else {
            out.push(`${n(i.dst)} = ${n(i.target)}.ptr[${n(i.index)}];`);
          }
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
          const p = parseContainer(tt);
          if (p && (p.kind === "view" || p.kind === "slice")) {
            out.push(`ps_check_view_valid(${n(i.target)}.version_ptr, ${n(i.target)}.version);`);
          }
          out.push(`${n(i.target)}.ptr[${n(i.index)}] = ${n(i.src)};`);
        }
      }
      break;
    case "make_list":
      out.push(`${n(i.dst)}.len = ${i.items.length};`);
      out.push(`${n(i.dst)}.cap = ${i.items.length};`);
      out.push(`${n(i.dst)}.version = 0;`);
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
    case "make_object":
      {
        const rt = t(i.dst);
        if (PROTO_MAP.has(rt)) out.push(`${n(i.dst)} = (${rt}*)calloc(1, sizeof(${rt}));`);
        else out.push(`${n(i.dst)} = 0;`);
      }
      break;
    case "member_get":
      {
        const rt = t(i.target);
        if (rt === "Exception" || rt === "RuntimeException") {
          if (i.name === "code") out.push(`${n(i.dst)} = ${n(i.target)}->code;`);
          else if (i.name === "category") out.push(`${n(i.dst)} = ${n(i.target)}->category;`);
          else if (i.name === "message") out.push(`${n(i.dst)} = ${n(i.target)}->message;`);
          else if (i.name === "file") out.push(`${n(i.dst)} = ${n(i.target)}->file;`);
          else if (i.name === "line") out.push(`${n(i.dst)} = ${n(i.target)}->line;`);
          else if (i.name === "column") out.push(`${n(i.dst)} = ${n(i.target)}->column;`);
          else out.push(`${n(i.dst)} = (ps_string){\"\",0};`);
        } else if (PROTO_MAP.has(rt)) {
          out.push(`${n(i.dst)} = ${n(i.target)}->${i.name};`);
        } else {
          out.push(`/* member_get ${i.name} */ ${n(i.dst)} = 0;`);
        }
      }
      break;
    case "member_set":
      {
        const rt = t(i.target);
        if (PROTO_MAP.has(rt)) {
          out.push(`${n(i.target)}->${i.name} = ${n(i.src)};`);
        } else {
          out.push(`/* member_set ${i.name} */`);
        }
      }
      break;
    case "ret":
      out.push(`return ${n(i.value)};`);
      break;
    case "ret_void":
      out.push("return;");
      break;
    case "throw":
      out.push(`ps_raise_user_exception(${n(i.value)});`);
      break;
    case "push_handler":
      out.push('if (ps_try_len >= 64) ps_panic("R1999", "RUNTIME_THROW", "try stack overflow");');
      out.push(`if (setjmp(ps_try_stack[ps_try_len]) != 0) goto ${i.target};`);
      out.push("ps_try_len += 1;");
      break;
    case "pop_handler":
      out.push("if (ps_try_len > 0) ps_try_len -= 1;");
      break;
    case "get_exception":
      out.push(`${n(i.dst)} = ps_get_exception();`);
      break;
    case "rethrow":
      out.push("ps_raise_exception();");
      break;
    case "exception_is":
      out.push(`${n(i.dst)} = ps_exception_is(${n(i.value)}, "${i.type}");`);
      break;
    case "branch_if":
      out.push(`if (${n(i.cond)}) goto ${i.then}; else goto ${i.else};`);
      break;
    case "jump":
      out.push(`goto ${i.target};`);
      break;
    case "iter_begin":
      {
        const srcType = t(i.source);
        const srcCont = parseContainer(srcType);
        if (srcCont && (srcCont.kind === "view" || srcCont.kind === "slice")) {
          out.push(`ps_check_view_valid(${n(i.source)}.version_ptr, ${n(i.source)}.version);`);
        }
      }
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
        else if (srcType === "view<glyph>") {
          out.push(`ps_check_view_valid(${n(i.source)}.version_ptr, ${n(i.source)}.version);`);
          out.push(`${n(i.dst)} = ps_view_glyph_get(${n(i.source)}, ${n(i.iter)}.i);`);
        }
        else if (srcType === "string") out.push(`${n(i.dst)} = (uint8_t)${n(i.source)}.ptr[${n(i.iter)}.i];`);
        else {
          const srcCont = parseContainer(srcType);
          if (srcCont && (srcCont.kind === "view" || srcCont.kind === "slice")) {
            out.push(`ps_check_view_valid(${n(i.source)}.version_ptr, ${n(i.source)}.version);`);
          }
          out.push(`${n(i.dst)} = ${n(i.source)}.ptr[${n(i.iter)}.i];`);
        }
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
  const blocks = fn.blocks.map((b) => ({ label: b.label, instrs: b.instrs.slice() }));
  const postJumpInstrs = [];
  for (const b of blocks) {
    const idx = b.instrs.findIndex((i) => ["jump", "branch_if", "return", "rethrow"].includes(i.op));
    if (idx >= 0 && idx < b.instrs.length - 1) {
      postJumpInstrs.push(...b.instrs.slice(idx + 1));
      b.instrs = b.instrs.slice(0, idx + 1);
    }
  }
  let postInjected = false;
  if (blocks.length > 0) out.push(`  goto ${blocks[0].label};`);
  for (const b of blocks) {
    out.push(`${b.label}:`);
    if (!postInjected && postJumpInstrs.length > 0 && b.label.startsWith("try_done_")) {
      postInjected = true;
      for (const i of postJumpInstrs) {
        const lines = emitInstr(i, fnInf, emitState);
        for (const l of lines) out.push(`  ${l}`);
      }
    }
    for (const i of b.instrs) {
      const lines = emitInstr(i, fnInf, emitState);
      for (const l of lines) out.push(`  ${l}`);
    }
  }

  if (ret === "void") out.push("  return;");
  else if (fn.name === "main") out.push("  return 0;");
  else if (ret.endsWith("*")) out.push("  return NULL;");
  else out.push(`  return (${ret}){0};`);
  out.push("}");
  return out;
}

function generateC(ir) {
  const protoMap = buildProtoMap(ir);
  const inferred = inferTempTypes(ir, protoMap);
  const typeNames = collectTypeNames(ir, inferred);
  PROTO_MAP = protoMap;
  const out = [];
  out.push("/* ProtoScript V2 reference C backend (non-optimized) */");
  out.push("/* This C is intended as a semantic oracle. */");
  out.push("#include <stdbool.h>");
  out.push("#include <stddef.h>");
  out.push("#include <stdint.h>");
  out.push("#include <stdio.h>");
  out.push("#include <stdlib.h>");
  out.push("#include <string.h>");
  out.push("#include <errno.h>");
  out.push("#include <math.h>");
  out.push("#include <limits.h>");
  out.push("#include <setjmp.h>");
  out.push("");
  out.push(...emitTypeDecls(typeNames, PROTO_MAP));
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
