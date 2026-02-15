"use strict";

const fs = require("fs");
const path = require("path");

let PROTO_MAP = new Map();
let VARIADIC_PARAM_INFO = new Map();
let VARARG_COUNTER = 0;
let JSON_TMP_COUNTER = 0;
let FN_NAMES = new Set();

function loadModuleRegistry() {
  const candidates = [
    process.env.PS_MODULE_REGISTRY,
    process.argv[1] ? path.join(path.dirname(process.argv[1]), "registry.json") : null,
    path.join(__dirname, "registry.json"),
    path.join(process.cwd(), "registry.json"),
    "/etc/ps/registry.json",
    "/usr/local/etc/ps/registry.json",
    "/opt/local/etc/ps/registry.json",
    path.join(process.cwd(), "modules", "registry.json"),
  ];
  for (const p of candidates) {
    try {
      if (!p || !fs.existsSync(p)) continue;
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
    case "TextFile":
    case "BinaryFile":
      return "ps_file*";
    case "JSONValue":
      return "ps_jsonvalue";
    case "Exception":
    case "RuntimeException":
      return "ps_exception*";
    case "Dir":
      return "ps_fs_dir*";
    case "Walker":
      return "ps_fs_walker*";
    case "RegExp":
      return "RegExp*";
    case "RegExpMatch":
      return "RegExpMatch*";
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
    const constStrings = new Map();
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
          const forceSet = (k, v) => {
            if (!k || !v) return;
            if (tempTypes.get(k) !== v) {
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
              if (i.literalType === "TextFile" || i.literalType === "BinaryFile") set(i.dst, i.literalType);
              else set(i.dst, i.literalType);
              if (i.literalType === "string") constStrings.set(i.dst, String(i.value));
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
              if (varTypes.has(i.name)) {
                if (!tempTypes.has(i.src)) {
                  set(i.src, varTypes.get(i.name));
                } else if (tempTypes.get(i.src) !== varTypes.get(i.name)) {
                  const vt = varTypes.get(i.name);
                  const st = tempTypes.get(i.src);
                  const vCont = parseContainer(vt);
                  const sCont = parseContainer(st);
                  if (
                    st === "unknown" ||
                    (vCont && sCont && vCont.kind === sCont.kind)
                  ) {
                    forceSet(i.src, vt);
                  }
                }
              }
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
              if (i.callee === "Io.openText") {
                set(i.dst, "TextFile");
              } else if (i.callee === "Io.openBinary") {
                set(i.dst, "BinaryFile");
              } else if (i.callee === "Fs.__dir_hasNext") {
                set(i.dst, "bool");
              } else if (i.callee === "Fs.__dir_next") {
                set(i.dst, "string");
              } else if (i.callee === "Fs.__dir_close" || i.callee === "Fs.__dir_reset") {
                set(i.dst, "void");
              } else if (i.callee === "Fs.__walker_hasNext") {
                set(i.dst, "bool");
              } else if (i.callee === "Fs.__walker_next") {
                set(i.dst, "PathEntry");
              } else if (i.callee === "Fs.__walker_close") {
                set(i.dst, "void");
              } else if (fnRet.has(i.callee)) set(i.dst, fnRet.get(i.callee));
              else if (MODULE_RETURNS.has(i.callee)) set(i.dst, MODULE_RETURNS.get(i.callee));
              else if (i.callee === "Exception") set(i.dst, "Exception");
              break;
            case "call_method_static":
              if (getType(i.receiver)) {
                const rt = getType(i.receiver);
                const rc = parseContainer(rt);
                if (rt === "TextFile" || rt === "BinaryFile") {
                  if (i.method === "read") {
                    if (rt === "BinaryFile") set(i.dst, "list<byte>");
                    else set(i.dst, "string");
                  } else if (i.method === "tell" || i.method === "size") {
                    set(i.dst, "int");
                  } else if (i.method === "name") {
                    set(i.dst, "string");
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
                  else if (i.method === "toInt") set(i.dst, "int");
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
                } else if (rc && rc.kind === "list" && i.method === "reverse") {
                  set(i.dst, "int");
                } else if (rc && rc.kind === "list" && i.method === "sort") {
                  set(i.dst, "int");
                } else if (rc && rc.kind === "list" && i.method === "removeLast") {
                  set(i.dst, "void");
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
              if (i.type && i.type.name) {
                set(i.dst, i.type.name);
              } else if (i.items.length > 0 && tempTypes.has(i.items[0])) {
                set(i.dst, `list<${tempTypes.get(i.items[0])}>`);
              }
              break;
            case "make_map":
              if (i.type && i.type.name) {
                set(i.dst, i.type.name);
              } else if (i.pairs.length > 0) {
                const first = i.pairs[0];
                if (getType(first.key) && getType(first.value)) {
                  const keyType = getType(first.key);
                  const valueType = getType(first.value);
                  let mixed = false;
                  for (let pi = 1; pi < i.pairs.length; pi += 1) {
                    const pk = i.pairs[pi];
                    if (getType(pk.key) !== keyType || getType(pk.value) !== valueType) {
                      mixed = true;
                      break;
                    }
                  }
                  if (mixed && keyType === "string") set(i.dst, "map<string,JSONValue>");
                  else set(i.dst, `map<${keyType},${valueType}>`);
                }
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

function protoParent(protoMap, name) {
  const p = protoMap.get(name);
  return p ? p.parent : null;
}

function protoIsSubtype(protoMap, child, parent) {
  if (!child || !parent) return false;
  if (child === parent) return true;
  let cur = protoMap.get(child);
  while (cur && cur.parent) {
    if (cur.parent === parent) return true;
    cur = cur.parent ? protoMap.get(cur.parent) : null;
  }
  return false;
}

function isExceptionProto(protoMap, name) {
  return protoIsSubtype(protoMap, name, "Exception");
}

function isRuntimeExceptionProto(protoMap, name) {
  return protoIsSubtype(protoMap, name, "RuntimeException");
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

function resolveCompareToOwner(protoMap, protoName) {
  let cur = protoName;
  while (cur) {
    if (FN_NAMES.has(`${cur}.compareTo`)) return cur;
    cur = protoParent(protoMap, cur);
  }
  return null;
}

function emitTypeDecls(typeNames, protoMap) {
  const out = [];
  out.push("typedef struct { const char* ptr; size_t len; } ps_string;");
  out.push("typedef struct { size_t i; size_t n; } ps_iter_cursor;");
  out.push("typedef struct ps_exception ps_exception;");
  out.push("struct ps_exception {");
  out.push("  const char* type;");
  out.push("  const char* parent;");
  out.push("  ps_string file;");
  out.push("  int64_t line;");
  out.push("  int64_t column;");
  out.push("  ps_string message;");
  out.push("  ps_exception* cause;");
  out.push("  ps_string code;");
  out.push("  ps_string category;");
  out.push("  int is_runtime;");
  out.push("};");
  out.push(
    "typedef struct { ps_string str; uint32_t* ptr; size_t len; size_t offset; int is_string; const uint64_t* version_ptr; uint64_t version; } ps_view_glyph;"
  );
  if (typeNames.has("JSONValue")) {
    out.push("typedef struct ps_jsonvalue ps_jsonvalue;");
  }
  out.push("typedef struct ps_fs_dir ps_fs_dir;");
  out.push("struct ps_fs_dir {");
  out.push("  DIR* dir;");
  out.push("  char* path;");
  out.push("  char* next;");
  out.push("  int done;");
  out.push("  int closed;");
  out.push("};");
  out.push("typedef struct ps_fs_walk_frame ps_fs_walk_frame;");
  out.push("struct ps_fs_walk_frame {");
  out.push("  DIR* dir;");
  out.push("  char* path;");
  out.push("  int depth;");
  out.push("};");
  out.push("typedef struct ps_fs_walker ps_fs_walker;");
  out.push("struct ps_fs_walker {");
  out.push("  ps_fs_walk_frame* stack;");
  out.push("  size_t len;");
  out.push("  size_t cap;");
  out.push("  int max_depth;");
  out.push("  int follow_symlinks;");
  out.push("  int closed;");
  out.push("  int has_next;");
  out.push("  struct {");
  out.push("    char* path;");
  out.push("    char* name;");
  out.push("    int depth;");
  out.push("    int is_dir;");
  out.push("    int is_file;");
  out.push("    int is_symlink;");
  out.push("  } next;");
  out.push("};");
  if (protoMap && protoMap.size > 0) {
    for (const name of protoMap.keys()) {
      out.push(`typedef struct ${name} ${name};`);
    }
  }
  if (!protoMap || !protoMap.has("CivilDateTime")) {
    out.push("typedef struct CivilDateTime CivilDateTime;");
    out.push("struct CivilDateTime {");
    out.push("  int64_t year;");
    out.push("  int64_t month;");
    out.push("  int64_t day;");
    out.push("  int64_t hour;");
    out.push("  int64_t minute;");
    out.push("  int64_t second;");
    out.push("  int64_t millisecond;");
    out.push("};");
  }
  if (!protoMap || !protoMap.has("RegExp")) {
    out.push("typedef struct RegExp RegExp;");
    out.push("struct RegExp {");
    out.push("  uint8_t __empty;");
    out.push("};");
  }
  if (!typeNames.has("list<string>")) {
    out.push("typedef struct { ps_string* ptr; size_t len; size_t cap; uint64_t version; } ps_list_string;");
  }
  if (!protoMap || !protoMap.has("RegExpMatch")) {
    out.push("typedef struct RegExpMatch RegExpMatch;");
    out.push("struct RegExpMatch {");
    out.push("  bool ok;");
    out.push("  int64_t start;");
    out.push("  int64_t end;");
    out.push("  ps_list_string groups;");
    out.push("};");
  }
  if (!typeNames.has("list<RegExpMatch>")) {
    out.push("typedef struct { RegExpMatch** ptr; size_t len; size_t cap; uint64_t version; } ps_list_RegExpMatch;");
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
  if (protoMap && protoMap.size > 0) {
    for (const [name, p] of protoMap.entries()) {
      if (name === "Exception" || name === "RuntimeException") continue;
      if (isExceptionProto(protoMap, name)) {
        out.push(`struct ${name} {`);
        out.push("  ps_exception base;");
        const fields = p.fields || [];
        if (fields.length === 0) {
          out.push("  uint8_t __empty;");
        } else {
          for (const f of fields) {
            const t = typeof f.type === "string" ? f.type : f.type?.name;
            out.push(`  ${cTypeFromName(t || "int")} ${f.name};`);
          }
        }
        out.push("};");
      } else {
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
    out.push(`static bool ps_map_remove_${m.base}(${mapC}* m, ${keyC} key) {`);
    out.push("  size_t idx = 0;");
    out.push(`  if (!ps_map_find_${m.base}(m, key, &idx)) return false;`);
    out.push("  for (size_t i = idx + 1; i < m->len; i += 1) {");
    out.push("    m->keys[i - 1] = m->keys[i];");
    out.push("    m->values[i - 1] = m->values[i];");
    out.push("  }");
    out.push("  m->len -= 1;");
    out.push("  return true;");
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

function emitRuntimeHelpers(protoMap) {
  const exceptionTypes = [];
  if (protoMap && protoMap.size > 0) {
    for (const name of protoMap.keys()) {
      if (isExceptionProto(protoMap, name)) exceptionTypes.push(name);
    }
  }
  const parentMap = new Map();
  for (const name of exceptionTypes) {
    const p = protoParent(protoMap, name);
    parentMap.set(name, p || null);
  }
  return [
    "static ps_exception ps_runtime_exception;",
    "static ps_exception* ps_last_exception = NULL;",
    "static jmp_buf ps_try_stack[64];",
    "static int ps_try_len = 0;",
    "extern char **environ;",
    "static ps_string ps_cstr(const char* s) {",
    "  ps_string out = { s ? s : \"\", s ? strlen(s) : 0 };",
    "  return out;",
    "}",
    "static bool ps_utf8_validate(const uint8_t* s, size_t len);",
    "static void ps_set_exception(const char* code, const char* category, const char* msg, int is_runtime) {",
    "  ps_runtime_exception.type = \"RuntimeException\";",
    "  ps_runtime_exception.parent = \"Exception\";",
    "  ps_runtime_exception.file = ps_cstr(\"<runtime>\");",
    "  ps_runtime_exception.line = 1;",
    "  ps_runtime_exception.column = 1;",
    "  ps_runtime_exception.message = ps_cstr(msg);",
    "  ps_runtime_exception.cause = NULL;",
    "  ps_runtime_exception.code = ps_cstr(code ? code : \"\");",
    "  ps_runtime_exception.category = ps_cstr(category ? category : \"\");",
    "  ps_runtime_exception.is_runtime = is_runtime;",
    "  ps_last_exception = &ps_runtime_exception;",
    "}",
    "static void ps_set_runtime_exception_typed(const char* type, const char* msg) {",
    "  ps_runtime_exception.type = type ? type : \"RuntimeException\";",
    "  ps_runtime_exception.parent = \"RuntimeException\";",
    "  ps_runtime_exception.file = ps_cstr(\"<runtime>\");",
    "  ps_runtime_exception.line = 1;",
    "  ps_runtime_exception.column = 1;",
    "  ps_runtime_exception.message = ps_cstr(msg ? msg : \"\");",
    "  ps_runtime_exception.cause = NULL;",
    "  ps_runtime_exception.code = ps_cstr(\"\");",
    "  ps_runtime_exception.category = ps_cstr(\"\");",
    "  ps_runtime_exception.is_runtime = 1;",
    "  ps_last_exception = &ps_runtime_exception;",
    "}",
    "static void ps_raise_exception(void) {",
    "  if (ps_try_len > 0) {",
    "    ps_try_len -= 1;",
    "    longjmp(ps_try_stack[ps_try_len], 1);",
    "  }",
    "  if (ps_last_exception) {",
    "    const char* file_ptr = ps_last_exception->file.ptr;",
    "    size_t file_len = ps_last_exception->file.len;",
    "    if (!file_ptr || file_len == 0) { file_ptr = \"<runtime>\"; file_len = strlen(file_ptr); }",
    "    long long line = ps_last_exception->line > 0 ? (long long)ps_last_exception->line : 1;",
    "    long long col = ps_last_exception->column > 0 ? (long long)ps_last_exception->column : 1;",
    "    if (ps_last_exception->is_runtime && ps_last_exception->code.len > 0 && ps_last_exception->category.len > 0) {",
    "      fprintf(stderr, \"%.*s:%lld:%lld %.*s %.*s: %.*s\\n\",",
    "              (int)file_len, file_ptr, line, col,",
    "              (int)ps_last_exception->code.len, ps_last_exception->code.ptr,",
    "              (int)ps_last_exception->category.len, ps_last_exception->category.ptr,",
    "              (int)ps_last_exception->message.len, ps_last_exception->message.ptr);",
    "    } else {",
    "      const char* type = ps_last_exception->type ? ps_last_exception->type : \"Exception\";",
    "      char got[256];",
    "      if (ps_last_exception->message.ptr && ps_last_exception->message.len > 0) {",
    "        snprintf(got, sizeof(got), \"%s(\\\"%.*s\\\")\", type, (int)ps_last_exception->message.len, ps_last_exception->message.ptr);",
    "      } else {",
    "        snprintf(got, sizeof(got), \"%s\", type);",
    "      }",
    "      char formatted[512];",
    "      snprintf(formatted, sizeof(formatted), \"unhandled exception. got %s; expected matching catch\", got);",
    "      fprintf(stderr, \"%.*s:%lld:%lld R1011 UNHANDLED_EXCEPTION: %s\\n\", (int)file_len, file_ptr, line, col, formatted);",
    "    }",
    "  }",
    "  exit(1);",
    "}",
    "static void ps_raise_runtime_typed(const char* type, const char* msg) {",
    "  ps_set_runtime_exception_typed(type, msg);",
    "  ps_raise_exception();",
    "}",
    "static void ps_panic(const char* code, const char* category, const char* msg) {",
    "  ps_set_exception(code, category, msg, 1);",
    "  ps_raise_exception();",
    "}",
    "static ps_exception* ps_get_exception(void) {",
    "  if (!ps_last_exception) ps_set_exception(\"R1999\", \"RUNTIME_THROW\", \"exception\", 1);",
    "  return ps_last_exception;",
    "}",
    "static const char* ps_exception_parent(const char* type) {",
    "  if (!type) return NULL;",
    ...Array.from(parentMap.entries()).map(([name, parent]) =>
      parent ? `  if (strcmp(type, \"${name}\") == 0) return \"${parent}\";` : `  if (strcmp(type, \"${name}\") == 0) return NULL;`
    ),
    "  return NULL;",
    "}",
    "static int ps_exception_is(ps_exception* ex, const char* type) {",
    "  if (!ex || !type) return 0;",
    "  if (strcmp(type, \"Exception\") == 0) return 1;",
    "  if (!ex->type) return 0;",
    "  const char* cur = ex->type;",
    "  while (cur) {",
    "    if (strcmp(cur, type) == 0) return 1;",
    "    cur = ps_exception_parent(cur);",
    "  }",
    "  return 0;",
    "}",
    "static ps_exception ps_exception_make(ps_string message) {",
    "  ps_exception ex;",
    "  ex.type = \"Exception\";",
    "  ex.parent = NULL;",
    "  ex.file = ps_cstr(\"\");",
    "  ex.line = 1;",
    "  ex.column = 1;",
    "  ex.message = message;",
    "  ex.cause = NULL;",
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
    "  if (ex) { ps_last_exception = ex; }",
    "  ps_raise_exception();",
    "}",
    "",
    "typedef struct { FILE* fp; int binary; int is_std; int closed; int readable; int writable; char* path; } ps_file;",
    "static ps_file ps_stdin = { NULL, 0, 1, 0, 1, 0, \"stdin\" };",
    "static ps_file ps_stdout = { NULL, 0, 1, 0, 0, 1, \"stdout\" };",
    "static ps_file ps_stderr = { NULL, 0, 1, 0, 0, 1, \"stderr\" };",
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
    "static void ps_raise_open_error(int err) {",
    "  if (err == ENOENT) ps_raise_runtime_typed(\"FileNotFoundException\", \"file not found\");",
    "  if (err == EACCES || err == EPERM) ps_raise_runtime_typed(\"PermissionDeniedException\", \"permission denied\");",
    "  if (err == ENOTDIR || err == EINVAL || err == ENAMETOOLONG) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  ps_raise_runtime_typed(\"FileOpenException\", \"open failed\");",
    "}",
    "static ps_file* ps_file_open_text(ps_string path, ps_string mode) {",
    "  ps_io_init();",
    "  char* cpath = ps_string_to_cstr(path);",
    "  char* cmode = ps_string_to_cstr(mode);",
    "  int readable = 0;",
    "  int writable = 0;",
    "  if (strcmp(cmode, \"r\") == 0) readable = 1;",
    "  else if (strcmp(cmode, \"w\") == 0) writable = 1;",
    "  else if (strcmp(cmode, \"a\") == 0) writable = 1;",
    "  else {",
    "    free(cpath);",
    "    free(cmode);",
    "    ps_raise_runtime_typed(\"InvalidModeException\", \"invalid mode\");",
    "  }",
    "  FILE* fp = fopen(cpath, cmode);",
    "  int err = errno;",
    "  free(cpath);",
    "  free(cmode);",
    "  if (!fp) ps_raise_open_error(err);",
    "  ps_file* f = (ps_file*)malloc(sizeof(ps_file));",
    "  if (!f) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  f->fp = fp;",
    "  f->binary = 0;",
    "  f->is_std = 0;",
    "  f->closed = 0;",
    "  f->readable = readable;",
    "  f->writable = writable;",
    "  f->path = ps_string_to_cstr(path);",
    "  return f;",
    "}",
    "static ps_file* ps_file_open_binary(ps_string path, ps_string mode) {",
    "  ps_io_init();",
    "  char* cpath = ps_string_to_cstr(path);",
    "  char* cmode = ps_string_to_cstr(mode);",
    "  const char* mapped = NULL;",
    "  int readable = 0;",
    "  int writable = 0;",
    "  if (strcmp(cmode, \"r\") == 0) { mapped = \"rb\"; readable = 1; }",
    "  else if (strcmp(cmode, \"w\") == 0) { mapped = \"wb\"; writable = 1; }",
    "  else if (strcmp(cmode, \"a\") == 0) { mapped = \"ab\"; writable = 1; }",
    "  if (!mapped) {",
    "    free(cpath);",
    "    free(cmode);",
    "    ps_raise_runtime_typed(\"InvalidModeException\", \"invalid mode\");",
    "  }",
    "  FILE* fp = fopen(cpath, mapped);",
    "  int err = errno;",
    "  free(cpath);",
    "  free(cmode);",
    "  if (!fp) ps_raise_open_error(err);",
    "  ps_file* f = (ps_file*)malloc(sizeof(ps_file));",
    "  if (!f) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  f->fp = fp;",
    "  f->binary = 1;",
    "  f->is_std = 0;",
    "  f->closed = 0;",
    "  f->readable = readable;",
    "  f->writable = writable;",
    "  f->path = ps_string_to_cstr(path);",
    "  return f;",
    "}",
    "static uint64_t ps_temp_seq = 0;",
    "static const char* ps_temp_dir(void) {",
    "  const char* dir = getenv(\"TMPDIR\");",
    "  if (!dir || dir[0] == '\\0') dir = \"/tmp\";",
    "  return dir;",
    "}",
    "static int ps_path_exists(const char* path) {",
    "  struct stat st;",
    "  return stat(path, &st) == 0;",
    "}",
    "static void ps_seed_rand(void) {",
    "  static int seeded = 0;",
    "  if (seeded) return;",
    "  seeded = 1;",
    "  unsigned int seed = (unsigned int)time(NULL);",
    "  seed ^= (unsigned int)getpid();",
    "  srand(seed);",
    "}",
    "static int ps_rand_bytes(uint8_t* out, size_t n) {",
    "  int fd = open(\"/dev/urandom\", O_RDONLY);",
    "  if (fd >= 0) {",
    "    size_t off = 0;",
    "    while (off < n) {",
    "      ssize_t r = read(fd, out + off, n - off);",
    "      if (r <= 0) break;",
    "      off += (size_t)r;",
    "    }",
    "    close(fd);",
    "    if (off == n) return 1;",
    "  }",
    "  ps_seed_rand();",
    "  for (size_t i = 0; i < n; i += 1) out[i] = (uint8_t)(rand() & 0xFF);",
    "  return 1;",
    "}",
    "static void ps_hex_encode(const uint8_t* in, size_t n, char* out) {",
    "  static const char* hex = \"0123456789abcdef\";",
    "  for (size_t i = 0; i < n; i += 1) {",
    "    out[i * 2] = hex[(in[i] >> 4) & 0xF];",
    "    out[i * 2 + 1] = hex[in[i] & 0xF];",
    "  }",
    "  out[n * 2] = '\\0';",
    "}",
    "static ps_string Io_tempPath(void) {",
    "  const char* dir = ps_temp_dir();",
    "  size_t dir_len = strlen(dir);",
    "  char sep[2] = {0, 0};",
    "  if (dir_len > 0) {",
    "    char last = dir[dir_len - 1];",
    "    if (last != '/' && last != '\\\\') {",
    "#ifdef _WIN32",
    "      sep[0] = '\\\\';",
    "#else",
    "      sep[0] = '/';",
    "#endif",
    "    }",
    "  }",
    "  const char* prefix = \"ps_\";",
    "  const int max_attempts = 128;",
    "  for (int attempt = 0; attempt < max_attempts; attempt += 1) {",
    "    uint8_t rnd[16];",
    "    if (!ps_rand_bytes(rnd, sizeof(rnd))) ps_raise_runtime_typed(\"IOException\", \"tempPath failed\");",
    "    char hex[33];",
    "    ps_hex_encode(rnd, sizeof(rnd), hex);",
    "    uint64_t seq = ps_temp_seq++;",
    "    char name[80];",
    "    snprintf(name, sizeof(name), \"%s%s_%llx\", prefix, hex, (unsigned long long)seq);",
    "    size_t path_len = dir_len + strlen(sep) + strlen(name);",
    "    char* full = (char*)malloc(path_len + 1);",
    "    if (!full) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    snprintf(full, path_len + 1, \"%s%s%s\", dir, sep, name);",
    "    if (!ps_path_exists(full)) return ps_string_from_owned(full);",
    "    free(full);",
    "  }",
    "  ps_raise_runtime_typed(\"IOException\", \"tempPath failed\");",
    "  return ps_cstr(\"\");",
    "}",
    "static void ps_file_check_open(ps_file* f) {",
    "  ps_io_init();",
    "  if (!f || f->closed || !f->fp) ps_raise_runtime_typed(\"FileClosedException\", \"file is closed\");",
    "}",
    "static void ps_file_close(ps_file* f) {",
    "  ps_file_check_open(f);",
    "  if (f->is_std) ps_raise_runtime_typed(\"StandardStreamCloseException\", \"cannot close standard stream\");",
    "  fclose(f->fp);",
    "  f->closed = 1;",
    "}",
    "static ps_string ps_file_name(ps_file* f) {",
    "  ps_file_check_open(f);",
    "  return ps_cstr(f->path ? f->path : \"\");",
    "}",
    "static void ps_file_write_text(ps_file* f, ps_string s) {",
    "  ps_file_check_open(f);",
    "  if (!f->writable) ps_raise_runtime_typed(\"WriteFailureException\", \"file not writable\");",
    "  if (f->binary) ps_raise_runtime_typed(\"InvalidArgumentException\", \"invalid write value\");",
    "  long start = ftell(f->fp);",
    "  size_t off = 0;",
    "  while (off < s.len) {",
    "    size_t n = fwrite(s.ptr + off, 1, s.len - off, f->fp);",
    "    if (n == 0 || ferror(f->fp)) {",
    "      if (start >= 0) fseek(f->fp, start, SEEK_SET);",
    "      ps_raise_runtime_typed(\"WriteFailureException\", \"write failed\");",
    "    }",
    "    off += n;",
    "  }",
    "}",
    "static void ps_file_write_bytes(ps_file* f, ps_list_byte b) {",
    "  ps_file_check_open(f);",
    "  if (!f->writable) ps_raise_runtime_typed(\"WriteFailureException\", \"file not writable\");",
    "  if (!f->binary) ps_raise_runtime_typed(\"InvalidArgumentException\", \"invalid write value\");",
    "  long start = ftell(f->fp);",
    "  size_t off = 0;",
    "  while (off < b.len) {",
    "    size_t n = fwrite(b.ptr + off, 1, b.len - off, f->fp);",
    "    if (n == 0 || ferror(f->fp)) {",
    "      if (start >= 0) fseek(f->fp, start, SEEK_SET);",
    "      ps_raise_runtime_typed(\"WriteFailureException\", \"write failed\");",
    "    }",
    "    off += n;",
    "  }",
    "}",
    "static ps_string ps_text_from_bytes(const uint8_t* buf, size_t n) {",
    "  for (size_t i = 0; i < n; i += 1) {",
    "    if (buf[i] == 0) ps_raise_runtime_typed(\"Utf8DecodeException\", \"invalid UTF-8\");",
    "  }",
    "  size_t out_len = n;",
    "  char* out = (char*)malloc(out_len + 1);",
    "  if (!out) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  if (out_len > 0) memcpy(out, buf, out_len);",
    "  out[out_len] = '\\0';",
    "  if (!ps_utf8_validate((const uint8_t*)out, out_len)) ps_raise_runtime_typed(\"Utf8DecodeException\", \"invalid UTF-8\");",
    "  return (ps_string){ out, out_len };",
    "}",
    "static int ps_read_utf8_glyph(FILE* fp, uint8_t out[4], size_t* out_len) {",
    "  int c0 = fgetc(fp);",
    "  if (c0 == EOF) return 0;",
    "  uint8_t b0 = (uint8_t)c0;",
    "  if (b0 == 0) ps_raise_runtime_typed(\"Utf8DecodeException\", \"invalid UTF-8\");",
    "  size_t len = 0;",
    "  uint32_t cp = 0;",
    "  if (b0 < 0x80) { len = 1; cp = b0; }",
    "  else if ((b0 & 0xE0) == 0xC0) { len = 2; cp = (uint32_t)(b0 & 0x1F); }",
    "  else if ((b0 & 0xF0) == 0xE0) { len = 3; cp = (uint32_t)(b0 & 0x0F); }",
    "  else if ((b0 & 0xF8) == 0xF0) { len = 4; cp = (uint32_t)(b0 & 0x07); }",
    "  else { ps_raise_runtime_typed(\"Utf8DecodeException\", \"invalid UTF-8\"); }",
    "  out[0] = b0;",
    "  for (size_t i = 1; i < len; i += 1) {",
    "    int ci = fgetc(fp);",
    "    if (ci == EOF) ps_raise_runtime_typed(\"Utf8DecodeException\", \"invalid UTF-8\");",
    "    uint8_t bi = (uint8_t)ci;",
    "    if ((bi & 0xC0) != 0x80) ps_raise_runtime_typed(\"Utf8DecodeException\", \"invalid UTF-8\");",
    "    out[i] = bi;",
    "    cp = (cp << 6) | (uint32_t)(bi & 0x3F);",
    "    if (bi == 0) ps_raise_runtime_typed(\"Utf8DecodeException\", \"invalid UTF-8\");",
    "  }",
    "  if (len == 2 && cp < 0x80) ps_raise_runtime_typed(\"Utf8DecodeException\", \"invalid UTF-8\");",
    "  if (len == 3 && cp < 0x800) ps_raise_runtime_typed(\"Utf8DecodeException\", \"invalid UTF-8\");",
    "  if (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ps_raise_runtime_typed(\"Utf8DecodeException\", \"invalid UTF-8\");",
    "  *out_len = len;",
    "  return 1;",
    "}",
    "static ps_string ps_file_read_size_glyphs(ps_file* f, int64_t size) {",
    "  ps_file_check_open(f);",
    "  if (!f->readable) ps_raise_runtime_typed(\"ReadFailureException\", \"file not readable\");",
    "  if (f->binary) ps_raise_runtime_typed(\"InvalidArgumentException\", \"read expects text file\");",
    "  if (size <= 0) ps_raise_runtime_typed(\"InvalidArgumentException\", \"invalid read size\");",
    "  size_t cap = (size_t)size * 4;",
    "  if (cap < 16) cap = 16;",
    "  uint8_t* buf = (uint8_t*)malloc(cap);",
    "  if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  size_t len = 0;",
    "  for (int64_t i = 0; i < size; i += 1) {",
    "    uint8_t g[4];",
    "    size_t glen = 0;",
    "    int r = ps_read_utf8_glyph(f->fp, g, &glen);",
    "    if (r == 0) break;",
    "    if (len + glen > cap) {",
    "      cap *= 2;",
    "      uint8_t* nb = (uint8_t*)realloc(buf, cap);",
    "      if (!nb) { free(buf); ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\"); }",
    "      buf = nb;",
    "    }",
    "    memcpy(buf + len, g, glen);",
    "    len += glen;",
    "  }",
    "  if (len == 0) { free(buf); return ps_cstr(\"\"); }",
    "  ps_string out = ps_text_from_bytes(buf, len);",
    "  free(buf);",
    "  return out;",
    "}",
    "static ps_list_byte ps_file_read_size_bytes(ps_file* f, int64_t size) {",
    "  ps_list_byte out = { NULL, 0, 0, 0 };",
    "  ps_file_check_open(f);",
    "  if (!f->readable) ps_raise_runtime_typed(\"ReadFailureException\", \"file not readable\");",
    "  if (!f->binary) ps_raise_runtime_typed(\"InvalidArgumentException\", \"read expects binary file\");",
    "  if (size <= 0) ps_raise_runtime_typed(\"InvalidArgumentException\", \"invalid read size\");",
    "  out.ptr = (uint8_t*)malloc((size_t)size);",
    "  if (!out.ptr) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  size_t n = fread(out.ptr, 1, (size_t)size, f->fp);",
    "  if (ferror(f->fp)) { free(out.ptr); ps_raise_runtime_typed(\"ReadFailureException\", \"read failed\"); }",
    "  if (n == 0) { free(out.ptr); out.ptr = NULL; out.len = 0; out.cap = 0; return out; }",
    "  out.len = n;",
    "  out.cap = n;",
    "  return out;",
    "}",
    "static int64_t ps_file_size_bytes(ps_file* f) {",
    "  ps_file_check_open(f);",
    "  long cur = ftell(f->fp);",
    "  fseek(f->fp, 0, SEEK_END);",
    "  long sz = ftell(f->fp);",
    "  if (cur >= 0) fseek(f->fp, cur, SEEK_SET);",
    "  if (sz < 0) sz = 0;",
    "  return (int64_t)sz;",
    "}",
    "static int64_t ps_file_tell_bytes(ps_file* f) {",
    "  ps_file_check_open(f);",
    "  long cur = ftell(f->fp);",
    "  if (cur < 0) ps_raise_runtime_typed(\"ReadFailureException\", \"tell failed\");",
    "  return (int64_t)cur;",
    "}",
    "static void ps_file_seek_bytes(ps_file* f, int64_t pos) {",
    "  ps_file_check_open(f);",
    "  if (pos < 0) ps_raise_runtime_typed(\"InvalidArgumentException\", \"invalid seek position\");",
    "  int64_t sz = ps_file_size_bytes(f);",
    "  if (pos > sz) ps_raise_runtime_typed(\"InvalidArgumentException\", \"seek out of range\");",
    "  fseek(f->fp, (long)pos, SEEK_SET);",
    "}",
    "static int64_t ps_file_size_glyphs(ps_file* f) {",
    "  ps_file_check_open(f);",
    "  if (f->binary) ps_raise_runtime_typed(\"InvalidArgumentException\", \"size expects text file\");",
    "  long cur = ftell(f->fp);",
    "  fseek(f->fp, 0, SEEK_SET);",
    "  int64_t count = 0;",
    "  while (1) {",
    "    uint8_t g[4];",
    "    size_t glen = 0;",
    "    int r = ps_read_utf8_glyph(f->fp, g, &glen);",
    "    if (r == 0) break;",
    "    count += 1;",
    "  }",
    "  if (cur >= 0) fseek(f->fp, cur, SEEK_SET);",
    "  return count;",
    "}",
    "static int64_t ps_file_tell_glyphs(ps_file* f) {",
    "  ps_file_check_open(f);",
    "  if (f->binary) ps_raise_runtime_typed(\"InvalidArgumentException\", \"tell expects text file\");",
    "  long cur = ftell(f->fp);",
    "  if (cur < 0) ps_raise_runtime_typed(\"ReadFailureException\", \"tell failed\");",
    "  fseek(f->fp, 0, SEEK_SET);",
    "  int64_t count = 0;",
    "  long pos = 0;",
    "  if (cur == 0) { fseek(f->fp, cur, SEEK_SET); return 0; }",
    "  while (1) {",
    "    uint8_t g[4];",
    "    size_t glen = 0;",
    "    int r = ps_read_utf8_glyph(f->fp, g, &glen);",
    "    if (r == 0) break;",
    "    pos += (long)glen;",
    "    count += 1;",
    "    if (pos == cur) { fseek(f->fp, cur, SEEK_SET); return count; }",
    "    if (pos > cur) break;",
    "  }",
    "  fseek(f->fp, cur, SEEK_SET);",
    "  ps_raise_runtime_typed(\"InvalidGlyphPositionException\", \"invalid tell position\");",
    "  return 0;",
    "}",
    "static void ps_file_seek_glyphs(ps_file* f, int64_t pos) {",
    "  ps_file_check_open(f);",
    "  if (f->binary) ps_raise_runtime_typed(\"InvalidArgumentException\", \"seek expects text file\");",
    "  if (pos < 0) ps_raise_runtime_typed(\"InvalidArgumentException\", \"invalid seek position\");",
    "  long cur = ftell(f->fp);",
    "  fseek(f->fp, 0, SEEK_SET);",
    "  if (pos == 0) { fseek(f->fp, 0, SEEK_SET); return; }",
    "  int64_t count = 0;",
    "  long byte_pos = 0;",
    "  while (1) {",
    "    uint8_t g[4];",
    "    size_t glen = 0;",
    "    int r = ps_read_utf8_glyph(f->fp, g, &glen);",
    "    if (r == 0) break;",
    "    count += 1;",
    "    byte_pos += (long)glen;",
    "    if (count == pos) { fseek(f->fp, byte_pos, SEEK_SET); return; }",
    "  }",
    "  if (cur >= 0) fseek(f->fp, cur, SEEK_SET);",
    "  ps_raise_runtime_typed(\"InvalidGlyphPositionException\", \"seek out of range\");",
    "}",
    "static ps_file* Io_openText(ps_string path, ps_string mode) {",
    "  return ps_file_open_text(path, mode);",
    "}",
    "static ps_file* Io_openBinary(ps_string path, ps_string mode) {",
    "  return ps_file_open_binary(path, mode);",
    "}",
    "static void Io_print(ps_string s) {",
    "  ps_io_init();",
    "  size_t off = 0;",
    "  while (off < s.len) {",
    "    size_t n = fwrite(s.ptr + off, 1, s.len - off, ps_stdout.fp);",
    "    if (n == 0 || ferror(ps_stdout.fp)) ps_raise_runtime_typed(\"WriteFailureException\", \"write failed\");",
    "    off += n;",
    "  }",
    "}",
    "static void Io_printLine(ps_string s) {",
    "  ps_io_init();",
    "  size_t off = 0;",
    "  while (off < s.len) {",
    "    size_t n = fwrite(s.ptr + off, 1, s.len - off, ps_stdout.fp);",
    "    if (n == 0 || ferror(ps_stdout.fp)) ps_raise_runtime_typed(\"WriteFailureException\", \"write failed\");",
    "    off += n;",
    "  }",
    "  if (fputc('\\n', ps_stdout.fp) == EOF) ps_raise_runtime_typed(\"WriteFailureException\", \"write failed\");",
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
    "static void ps_map_set_string_JSONValue(ps_map_string_JSONValue* m, ps_string key, ps_jsonvalue value);",
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
    "  ps_map_string_JSONValue out = (ps_map_string_JSONValue){ NULL, NULL, 0, 0 };",
    "  if (p->i < p->len && p->s[p->i] == '}') { p->i += 1; return JSON_object(out); }",
    "  while (p->i < p->len) {",
    "    if (p->s[p->i] != '\"') { *ok = 0; return JSON_null(); }",
    "    ps_jsonvalue key = ps_json_parse_string(p, ok);",
    "    if (!*ok) return JSON_null();",
    "    ps_json_skip_ws(p);",
    "    if (p->i >= p->len || p->s[p->i] != ':') { *ok = 0; return JSON_null(); }",
    "    p->i += 1;",
    "    ps_jsonvalue val = ps_json_parse_value(p, ok);",
    "    if (!*ok) return JSON_null();",
    "    ps_map_set_string_JSONValue(&out, ps_json_as_string(key), val);",
    "    ps_json_skip_ws(p);",
    "    if (p->i < p->len && p->s[p->i] == ',') { p->i += 1; ps_json_skip_ws(p); continue; }",
    "    if (p->i < p->len && p->s[p->i] == '}') { p->i += 1; break; }",
    "    *ok = 0; return JSON_null();",
    "  }",
    "  if (p->i > p->len) { *ok = 0; return JSON_null(); }",
    "  return JSON_object(out);",
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
    "typedef struct { char* ptr; size_t len; size_t cap; } ps_json_buf;",
    "static void ps_json_buf_push(ps_json_buf* b, uint8_t c) {",
    "  if (b->len + 1 > b->cap) {",
    "    size_t nc = b->cap == 0 ? 64 : b->cap * 2;",
    "    while (nc < b->len + 1) nc *= 2;",
    "    b->ptr = (char*)realloc(b->ptr, nc);",
    "    if (!b->ptr) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    b->cap = nc;",
    "  }",
    "  b->ptr[b->len++] = (char)c;",
    "}",
    "static void ps_json_buf_push_utf8(ps_json_buf* b, uint32_t cp) {",
    "  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) ps_panic(\"R1010\", \"RUNTIME_JSON_ERROR\", \"invalid unicode escape\");",
    "  if (cp <= 0x7F) {",
    "    ps_json_buf_push(b, (uint8_t)cp);",
    "  } else if (cp <= 0x7FF) {",
    "    ps_json_buf_push(b, (uint8_t)(0xC0 | (cp >> 6)));",
    "    ps_json_buf_push(b, (uint8_t)(0x80 | (cp & 0x3F)));",
    "  } else if (cp <= 0xFFFF) {",
    "    ps_json_buf_push(b, (uint8_t)(0xE0 | (cp >> 12)));",
    "    ps_json_buf_push(b, (uint8_t)(0x80 | ((cp >> 6) & 0x3F)));",
    "    ps_json_buf_push(b, (uint8_t)(0x80 | (cp & 0x3F)));",
    "  } else {",
    "    ps_json_buf_push(b, (uint8_t)(0xF0 | (cp >> 18)));",
    "    ps_json_buf_push(b, (uint8_t)(0x80 | ((cp >> 12) & 0x3F)));",
    "    ps_json_buf_push(b, (uint8_t)(0x80 | ((cp >> 6) & 0x3F)));",
    "    ps_json_buf_push(b, (uint8_t)(0x80 | (cp & 0x3F)));",
    "  }",
    "}",
    "static int ps_json_hex4(ps_json_parser* p, uint32_t* out) {",
    "  if (p->i + 4 > p->len) return 0;",
    "  uint32_t v = 0;",
    "  for (int i = 0; i < 4; i += 1) {",
    "    char c = p->s[p->i + i];",
    "    uint32_t d = 0;",
    "    if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');",
    "    else if (c >= 'a' && c <= 'f') d = 10u + (uint32_t)(c - 'a');",
    "    else if (c >= 'A' && c <= 'F') d = 10u + (uint32_t)(c - 'A');",
    "    else return 0;",
    "    v = (v << 4) | d;",
    "  }",
    "  p->i += 4;",
    "  *out = v;",
    "  return 1;",
    "}",
    "static ps_jsonvalue ps_json_parse_string(ps_json_parser* p, int* ok) {",
    "  p->i += 1;",
    "  ps_json_buf buf = { NULL, 0, 0 };",
    "  while (p->i < p->len) {",
    "    unsigned char c = (unsigned char)p->s[p->i++];",
    "    if (c == '\"') {",
    "      if (!buf.ptr) buf.ptr = (char*)malloc(1);",
    "      if (!buf.ptr) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "      buf.ptr[buf.len] = '\\0';",
    "      if (!ps_utf8_validate((const uint8_t*)buf.ptr, buf.len)) ps_panic(\"R1007\", \"RUNTIME_INVALID_UTF8\", \"invalid UTF-8\");",
    "      return JSON_string((ps_string){ buf.ptr, buf.len });",
    "    }",
    "    if (c < 0x20) { *ok = 0; return JSON_null(); }",
    "    if (c == '\\\\') {",
    "      if (p->i >= p->len) { *ok = 0; return JSON_null(); }",
    "      char esc = p->s[p->i++];",
    "      switch (esc) {",
    "        case '\"': ps_json_buf_push(&buf, '\"'); break;",
    "        case '\\\\': ps_json_buf_push(&buf, '\\\\'); break;",
    "        case '/': ps_json_buf_push(&buf, '/'); break;",
    "        case 'b': ps_json_buf_push(&buf, 0x08); break;",
    "        case 'f': ps_json_buf_push(&buf, 0x0C); break;",
    "        case 'n': ps_json_buf_push(&buf, '\\n'); break;",
    "        case 'r': ps_json_buf_push(&buf, '\\r'); break;",
    "        case 't': ps_json_buf_push(&buf, '\\t'); break;",
    "        case 'u': {",
    "          uint32_t cp = 0;",
    "          if (!ps_json_hex4(p, &cp)) { *ok = 0; return JSON_null(); }",
    "          if (cp >= 0xD800 && cp <= 0xDBFF) {",
    "            if (p->i + 2 > p->len || p->s[p->i] != '\\\\' || p->s[p->i + 1] != 'u') { *ok = 0; return JSON_null(); }",
    "            p->i += 2;",
    "            uint32_t low = 0;",
    "            if (!ps_json_hex4(p, &low)) { *ok = 0; return JSON_null(); }",
    "            if (low < 0xDC00 || low > 0xDFFF) { *ok = 0; return JSON_null(); }",
    "            cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));",
    "          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {",
    "            *ok = 0; return JSON_null();",
    "          }",
    "          ps_json_buf_push_utf8(&buf, cp);",
    "          break;",
    "        }",
    "        default: *ok = 0; return JSON_null();",
    "      }",
    "      continue;",
    "    }",
    "    ps_json_buf_push(&buf, c);",
    "  }",
    "  *ok = 0; return JSON_null();",
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
    "static ps_exception* ps_exception_new_typed(const char* type, ps_string message) {",
    "  ps_exception* ex = (ps_exception*)malloc(sizeof(ps_exception));",
    "  if (!ex) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  ex->type = type ? type : \"Exception\";",
    "  ex->parent = ps_exception_parent(ex->type);",
    "  ex->file = ps_cstr(\"\");",
    "  ex->line = 1;",
    "  ex->column = 1;",
    "  ex->message = message;",
    "  ex->cause = NULL;",
    "  ex->code = ps_cstr(\"\");",
    "  ex->category = ps_cstr(\"\");",
    "  ex->is_runtime = 0;",
    "  return ex;",
    "}",
    "static void ps_raise_typed(const char* type, ps_string message) {",
    "  ps_exception* ex = ps_exception_new_typed(type, message);",
    "  ps_raise_user_exception(ex);",
    "}",
    "static int64_t ps_days_from_civil(int64_t y, int64_t m, int64_t d) {",
    "  y -= (m <= 2);",
    "  int64_t era = (y >= 0 ? y : y - 399) / 400;",
    "  int64_t yoe = y - era * 400;",
    "  int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;",
    "  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;",
    "  return era * 146097 + doe - 719468;",
    "}",
    "static int ps_is_leap_year(int64_t y) {",
    "  if (y % 400 == 0) return 1;",
    "  if (y % 100 == 0) return 0;",
    "  return (y % 4) == 0;",
    "}",
    "static int ps_days_in_month(int64_t y, int64_t m) {",
    "  if (m == 2) return ps_is_leap_year(y) ? 29 : 28;",
    "  if (m == 4 || m == 6 || m == 9 || m == 11) return 30;",
    "  return 31;",
    "}",
    "static int ps_tz_string_valid(ps_string tz) {",
    "  if (!tz.ptr || tz.len == 0) return 0;",
    "  for (size_t i = 0; i < tz.len; i += 1) {",
    "    unsigned char c = (unsigned char)tz.ptr[i];",
    "    if (isspace(c)) return 0;",
    "    if (!(isalnum(c) || c == '_' || c == '+' || c == '-' || c == '/')) return 0;",
    "  }",
    "  return 1;",
    "}",
    "static int ps_tz_path_exists(ps_string tz) {",
    "  const char* tzdir = getenv(\"TZDIR\");",
    "  const char* dirs[] = { tzdir, \"/usr/share/zoneinfo\", \"/usr/share/lib/zoneinfo\", \"/usr/lib/zoneinfo\", NULL };",
    "  char* name = ps_string_to_cstr(tz);",
    "  char path[4096];",
    "  int ok = 0;",
    "  for (size_t i = 0; dirs[i]; i += 1) {",
    "    const char* dir = dirs[i];",
    "    if (!dir || !*dir) continue;",
    "    size_t need = strlen(dir) + 1 + strlen(name) + 1;",
    "    if (need >= sizeof(path)) continue;",
    "    snprintf(path, sizeof(path), \"%s/%s\", dir, name);",
    "    struct stat st;",
    "    if (stat(path, &st) == 0) { ok = 1; break; }",
    "  }",
    "  free(name);",
    "  return ok;",
    "}",
    "typedef struct { char* prev; int had_prev; } ps_tz_state;",
    "static int ps_tz_push(ps_string tz, ps_tz_state* st) {",
    "  st->prev = NULL; st->had_prev = 0;",
    "  const char* prev = getenv(\"TZ\");",
    "  if (prev) { st->prev = strdup(prev); st->had_prev = 1; }",
    "  char* name = ps_string_to_cstr(tz);",
    "  int ok = (setenv(\"TZ\", name, 1) == 0);",
    "  free(name);",
    "  tzset();",
    "  return ok;",
    "}",
    "static void ps_tz_pop(ps_tz_state* st) {",
    "  if (st->had_prev && st->prev) setenv(\"TZ\", st->prev, 1);",
    "  else unsetenv(\"TZ\");",
    "  tzset();",
    "  free(st->prev);",
    "  st->prev = NULL;",
    "  st->had_prev = 0;",
    "}",
    "static void ps_split_epoch_ms(int64_t epoch_ms, time_t* sec, int64_t* ms) {",
    "  int64_t s = epoch_ms / 1000;",
    "  int64_t m = epoch_ms % 1000;",
    "  if (m < 0) { m += 1000; s -= 1; }",
    "  *sec = (time_t)s;",
    "  *ms = m;",
    "}",
    "static int64_t ps_epoch_seconds_utc(int64_t y, int64_t m, int64_t d, int64_t hh, int64_t mm, int64_t ss) {",
    "  int64_t days = ps_days_from_civil(y, m, d);",
    "  return days * 86400 + hh * 3600 + mm * 60 + ss;",
    "}",
    "static int64_t ps_offset_seconds_for_epoch(int64_t epoch_ms, ps_string tz) {",
    "  time_t sec; int64_t ms;",
    "  ps_split_epoch_ms(epoch_ms, &sec, &ms);",
    "  (void)ms;",
    "  struct tm lt;",
    "  ps_tz_state st;",
    "  if (!ps_tz_push(tz, &st)) ps_panic(\"R1010\", \"RUNTIME_TIME_ERROR\", \"invalid time zone\");",
    "  if (!localtime_r(&sec, &lt)) { ps_tz_pop(&st); ps_panic(\"R1010\", \"RUNTIME_TIME_ERROR\", \"invalid epoch\"); }",
    "  ps_tz_pop(&st);",
    "  int64_t local_sec = ps_epoch_seconds_utc((int64_t)lt.tm_year + 1900, (int64_t)lt.tm_mon + 1, (int64_t)lt.tm_mday,",
    "                                           (int64_t)lt.tm_hour, (int64_t)lt.tm_min, (int64_t)lt.tm_sec);",
    "  return local_sec - (int64_t)sec;",
    "}",
    "static int ps_day_of_week(int64_t y, int64_t m, int64_t d) {",
    "  int64_t days = ps_days_from_civil(y, m, d);",
    "  int64_t w = (days + 4) % 7;",
    "  if (w < 0) w += 7;",
    "  return w == 0 ? 7 : (int)w;",
    "}",
    "static int ps_day_of_year(int64_t y, int64_t m, int64_t d) {",
    "  int total = 0;",
    "  for (int i = 1; i < (int)m; i += 1) total += ps_days_in_month(y, i);",
    "  return total + (int)d;",
    "}",
    "static int ps_weeks_in_iso_year(int64_t y) {",
    "  int jan1 = ps_day_of_week(y, 1, 1);",
    "  if (jan1 == 4) return 53;",
    "  if (jan1 == 3 && ps_is_leap_year(y)) return 53;",
    "  return 52;",
    "}",
    "static void ps_iso_week_info(int64_t y, int64_t m, int64_t d, int* week, int* week_year) {",
    "  int dow = ps_day_of_week(y, m, d);",
    "  int doy = ps_day_of_year(y, m, d);",
    "  int w = (doy - dow + 10) / 7;",
    "  int wy = (int)y;",
    "  if (w < 1) { wy = (int)y - 1; w = ps_weeks_in_iso_year(wy); }",
    "  else { int weeks = ps_weeks_in_iso_year(y); if (w > weeks) { wy = (int)y + 1; w = 1; } }",
    "  *week = w; *week_year = wy;",
    "}",
    "static int64_t Time_nowEpochMillis(void) {",
    "  struct timespec ts;",
    "  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) ps_panic(\"R1010\", \"RUNTIME_TIME_ERROR\", \"clock_gettime failed\");",
    "  return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);",
    "}",
    "static int64_t Time_nowMonotonicNanos(void) {",
    "  struct timespec ts;",
    "  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) ps_panic(\"R1010\", \"RUNTIME_TIME_ERROR\", \"clock_gettime failed\");",
    "  return (int64_t)ts.tv_sec * 1000000000 + (int64_t)ts.tv_nsec;",
    "}",
    "static void Time_sleepMillis(int64_t ms) {",
    "  if (ms <= 0) return;",
    "  struct timespec ts;",
    "  ts.tv_sec = (time_t)(ms / 1000);",
    "  ts.tv_nsec = (long)((ms % 1000) * 1000000);",
    "  while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}",
    "}",
    "static CivilDateTime* TimeCivil_fromEpochUTC(int64_t epoch_ms) {",
    "  time_t sec; int64_t ms;",
    "  ps_split_epoch_ms(epoch_ms, &sec, &ms);",
    "  struct tm tm;",
    "  if (!gmtime_r(&sec, &tm)) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid epoch\"));",
    "  CivilDateTime* dt = (CivilDateTime*)calloc(1, sizeof(CivilDateTime));",
    "  if (!dt) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  dt->year = (int64_t)tm.tm_year + 1900;",
    "  dt->month = (int64_t)tm.tm_mon + 1;",
    "  dt->day = (int64_t)tm.tm_mday;",
    "  dt->hour = (int64_t)tm.tm_hour;",
    "  dt->minute = (int64_t)tm.tm_min;",
    "  dt->second = (int64_t)tm.tm_sec;",
    "  dt->millisecond = ms;",
    "  return dt;",
    "}",
    "static int64_t TimeCivil_toEpochUTC(CivilDateTime* dt) {",
    "  if (!dt) ps_panic(\"R1010\", \"RUNTIME_TYPE_ERROR\", \"invalid CivilDateTime\");",
    "  if (dt->month < 1 || dt->month > 12) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid month\"));",
    "  if (dt->hour < 0 || dt->hour > 23) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid hour\"));",
    "  if (dt->minute < 0 || dt->minute > 59) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid minute\"));",
    "  if (dt->second < 0 || dt->second > 59) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid second\"));",
    "  if (dt->millisecond < 0 || dt->millisecond > 999) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid millisecond\"));",
    "  int dim = ps_days_in_month(dt->year, dt->month);",
    "  if (dt->day < 1 || dt->day > dim) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid day\"));",
    "  __int128 sec = (__int128)ps_epoch_seconds_utc(dt->year, dt->month, dt->day, dt->hour, dt->minute, dt->second);",
    "  __int128 total = sec * 1000 + dt->millisecond;",
    "  if (total < INT64_MIN || total > INT64_MAX) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"date out of range\"));",
    "  return (int64_t)total;",
    "}",
    "static CivilDateTime* TimeCivil_fromEpoch(int64_t epoch_ms, ps_string tz) {",
    "  if (!ps_tz_string_valid(tz) || !ps_tz_path_exists(tz)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  time_t sec; int64_t ms;",
    "  ps_split_epoch_ms(epoch_ms, &sec, &ms);",
    "  struct tm tm;",
    "  ps_tz_state st;",
    "  if (!ps_tz_push(tz, &st)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  if (!localtime_r(&sec, &tm)) { ps_tz_pop(&st); ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid epoch\")); }",
    "  ps_tz_pop(&st);",
    "  CivilDateTime* dt = (CivilDateTime*)calloc(1, sizeof(CivilDateTime));",
    "  if (!dt) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  dt->year = (int64_t)tm.tm_year + 1900;",
    "  dt->month = (int64_t)tm.tm_mon + 1;",
    "  dt->day = (int64_t)tm.tm_mday;",
    "  dt->hour = (int64_t)tm.tm_hour;",
    "  dt->minute = (int64_t)tm.tm_min;",
    "  dt->second = (int64_t)tm.tm_sec;",
    "  dt->millisecond = ms;",
    "  return dt;",
    "}",
    "static int ps_match_local(struct tm* tm, CivilDateTime* dt) {",
    "  if ((int64_t)tm->tm_year + 1900 != dt->year) return 0;",
    "  if ((int64_t)tm->tm_mon + 1 != dt->month) return 0;",
    "  if ((int64_t)tm->tm_mday != dt->day) return 0;",
    "  if ((int64_t)tm->tm_hour != dt->hour) return 0;",
    "  if ((int64_t)tm->tm_min != dt->minute) return 0;",
    "  if ((int64_t)tm->tm_sec != dt->second) return 0;",
    "  return 1;",
    "}",
    "static int ps_candidate_epoch(time_t* out, CivilDateTime* dt, int isdst) {",
    "  struct tm tm;",
    "  memset(&tm, 0, sizeof(tm));",
    "  tm.tm_year = (int)(dt->year - 1900);",
    "  tm.tm_mon = (int)(dt->month - 1);",
    "  tm.tm_mday = (int)dt->day;",
    "  tm.tm_hour = (int)dt->hour;",
    "  tm.tm_min = (int)dt->minute;",
    "  tm.tm_sec = (int)dt->second;",
    "  tm.tm_isdst = isdst;",
    "  time_t t = mktime(&tm);",
    "  struct tm chk;",
    "  if (!localtime_r(&t, &chk)) return 0;",
    "  if (!ps_match_local(&chk, dt)) return 0;",
    "  *out = t;",
    "  return 1;",
    "}",
    "static int64_t TimeCivil_toEpoch(CivilDateTime* dt, ps_string tz, int64_t strategy) {",
    "  if (!dt) ps_panic(\"R1010\", \"RUNTIME_TYPE_ERROR\", \"invalid CivilDateTime\");",
    "  if (!ps_tz_string_valid(tz) || !ps_tz_path_exists(tz)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  if (!(strategy == 0 || strategy == 1 || strategy == 2)) ps_panic(\"R1010\", \"RUNTIME_TYPE_ERROR\", \"invalid DST strategy\");",
    "  if (dt->month < 1 || dt->month > 12) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid month\"));",
    "  if (dt->hour < 0 || dt->hour > 23) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid hour\"));",
    "  if (dt->minute < 0 || dt->minute > 59) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid minute\"));",
    "  if (dt->second < 0 || dt->second > 59) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid second\"));",
    "  if (dt->millisecond < 0 || dt->millisecond > 999) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid millisecond\"));",
    "  int dim = ps_days_in_month(dt->year, dt->month);",
    "  if (dt->day < 1 || dt->day > dim) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid day\"));",
    "  time_t t1 = 0, t2 = 0;",
    "  int ok1 = 0, ok2 = 0;",
    "  ps_tz_state st;",
    "  if (!ps_tz_push(tz, &st)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  ok1 = ps_candidate_epoch(&t1, dt, 0);",
    "  ok2 = ps_candidate_epoch(&t2, dt, 1);",
    "  ps_tz_pop(&st);",
    "  if (!ok1 && !ok2) ps_raise_typed(\"DSTNonExistentTimeException\", ps_cstr(\"non-existent DST time\"));",
    "  if (ok1 && ok2 && t1 == t2) ok2 = 0;",
    "  if (ok1 && ok2) {",
    "    if (strategy == 2) ps_raise_typed(\"DSTAmbiguousTimeException\", ps_cstr(\"ambiguous DST time\"));",
    "    time_t chosen = (strategy == 0) ? (t1 < t2 ? t1 : t2) : (t1 > t2 ? t1 : t2);",
    "    return (int64_t)chosen * 1000 + dt->millisecond;",
    "  }",
    "  return (int64_t)(ok1 ? t1 : t2) * 1000 + dt->millisecond;",
    "}",
    "static bool TimeCivil_isDST(int64_t epoch_ms, ps_string tz) {",
    "  if (!ps_tz_string_valid(tz) || !ps_tz_path_exists(tz)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  int64_t off = ps_offset_seconds_for_epoch(epoch_ms, tz);",
    "  int64_t jan = ps_epoch_seconds_utc(2024, 1, 15, 0, 0, 0) * 1000;",
    "  int64_t jul = ps_epoch_seconds_utc(2024, 7, 15, 0, 0, 0) * 1000;",
    "  int64_t off_jan = ps_offset_seconds_for_epoch(jan, tz);",
    "  int64_t off_jul = ps_offset_seconds_for_epoch(jul, tz);",
    "  int64_t std = off_jan < off_jul ? off_jan : off_jul;",
    "  return off != std;",
    "}",
    "static int64_t TimeCivil_offsetSeconds(int64_t epoch_ms, ps_string tz) {",
    "  if (!ps_tz_string_valid(tz) || !ps_tz_path_exists(tz)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  return ps_offset_seconds_for_epoch(epoch_ms, tz);",
    "}",
    "static int64_t TimeCivil_standardOffsetSeconds(ps_string tz) {",
    "  if (!ps_tz_string_valid(tz) || !ps_tz_path_exists(tz)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  int64_t jan = ps_epoch_seconds_utc(2024, 1, 15, 0, 0, 0) * 1000;",
    "  int64_t jul = ps_epoch_seconds_utc(2024, 7, 15, 0, 0, 0) * 1000;",
    "  int64_t off_jan = ps_offset_seconds_for_epoch(jan, tz);",
    "  int64_t off_jul = ps_offset_seconds_for_epoch(jul, tz);",
    "  return off_jan < off_jul ? off_jan : off_jul;",
    "}",
    "static int64_t TimeCivil_dayOfWeek(int64_t epoch_ms, ps_string tz) {",
    "  if (!ps_tz_string_valid(tz) || !ps_tz_path_exists(tz)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  time_t sec; int64_t ms;",
    "  ps_split_epoch_ms(epoch_ms, &sec, &ms);",
    "  (void)ms;",
    "  struct tm tm;",
    "  ps_tz_state st;",
    "  if (!ps_tz_push(tz, &st)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  if (!localtime_r(&sec, &tm)) { ps_tz_pop(&st); ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid epoch\")); }",
    "  ps_tz_pop(&st);",
    "  return (int64_t)ps_day_of_week((int64_t)tm.tm_year + 1900, (int64_t)tm.tm_mon + 1, (int64_t)tm.tm_mday);",
    "}",
    "static int64_t TimeCivil_dayOfYear(int64_t epoch_ms, ps_string tz) {",
    "  if (!ps_tz_string_valid(tz) || !ps_tz_path_exists(tz)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  time_t sec; int64_t ms;",
    "  ps_split_epoch_ms(epoch_ms, &sec, &ms);",
    "  (void)ms;",
    "  struct tm tm;",
    "  ps_tz_state st;",
    "  if (!ps_tz_push(tz, &st)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  if (!localtime_r(&sec, &tm)) { ps_tz_pop(&st); ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid epoch\")); }",
    "  ps_tz_pop(&st);",
    "  return (int64_t)ps_day_of_year((int64_t)tm.tm_year + 1900, (int64_t)tm.tm_mon + 1, (int64_t)tm.tm_mday);",
    "}",
    "static int64_t TimeCivil_weekOfYearISO(int64_t epoch_ms, ps_string tz) {",
    "  if (!ps_tz_string_valid(tz) || !ps_tz_path_exists(tz)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  time_t sec; int64_t ms;",
    "  ps_split_epoch_ms(epoch_ms, &sec, &ms);",
    "  (void)ms;",
    "  struct tm tm;",
    "  ps_tz_state st;",
    "  if (!ps_tz_push(tz, &st)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  if (!localtime_r(&sec, &tm)) { ps_tz_pop(&st); ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid epoch\")); }",
    "  ps_tz_pop(&st);",
    "  int week = 0, wy = 0;",
    "  ps_iso_week_info((int64_t)tm.tm_year + 1900, (int64_t)tm.tm_mon + 1, (int64_t)tm.tm_mday, &week, &wy);",
    "  return (int64_t)week;",
    "}",
    "static int64_t TimeCivil_weekYearISO(int64_t epoch_ms, ps_string tz) {",
    "  if (!ps_tz_string_valid(tz) || !ps_tz_path_exists(tz)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  time_t sec; int64_t ms;",
    "  ps_split_epoch_ms(epoch_ms, &sec, &ms);",
    "  (void)ms;",
    "  struct tm tm;",
    "  ps_tz_state st;",
    "  if (!ps_tz_push(tz, &st)) ps_raise_typed(\"InvalidTimeZoneException\", ps_cstr(\"invalid time zone\"));",
    "  if (!localtime_r(&sec, &tm)) { ps_tz_pop(&st); ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid epoch\")); }",
    "  ps_tz_pop(&st);",
    "  int week = 0, wy = 0;",
    "  ps_iso_week_info((int64_t)tm.tm_year + 1900, (int64_t)tm.tm_mon + 1, (int64_t)tm.tm_mday, &week, &wy);",
    "  return (int64_t)wy;",
    "}",
    "static bool TimeCivil_isLeapYear(int64_t y) { return ps_is_leap_year(y); }",
    "static int64_t TimeCivil_daysInMonth(int64_t y, int64_t m) {",
    "  if (m < 1 || m > 12) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid month\"));",
    "  return (int64_t)ps_days_in_month(y, m);",
    "}",
    "static int64_t TimeCivil_parseISO8601(ps_string s) {",
    "  const char* p = s.ptr; size_t len = s.len;",
    "  if (!p || len < 10) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "  int year=0, month=0, day=0;",
    "  if (len < 10) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "  if (!(p[4] == '-' && p[7] == '-')) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "  if (!(p[0]>='0'&&p[0]<='9'&&p[1]>='0'&&p[1]<='9'&&p[2]>='0'&&p[2]<='9'&&p[3]>='0'&&p[3]<='9')) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "  year = (p[0]-'0')*1000+(p[1]-'0')*100+(p[2]-'0')*10+(p[3]-'0');",
    "  if (!(p[5]>='0'&&p[5]<='9'&&p[6]>='0'&&p[6]<='9')) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "  if (!(p[8]>='0'&&p[8]<='9'&&p[9]>='0'&&p[9]<='9')) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "  month = (p[5]-'0')*10+(p[6]-'0');",
    "  day = (p[8]-'0')*10+(p[9]-'0');",
    "  int hour=0, minute=0, second=0, ms=0;",
    "  size_t i = 10;",
    "  if (i < len) {",
    "    if (p[i] != 'T') ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "    if (i + 8 >= len) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "    if (!(p[i+1]>='0'&&p[i+1]<='9'&&p[i+2]>='0'&&p[i+2]<='9')) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "    if (!(p[i+4]>='0'&&p[i+4]<='9'&&p[i+5]>='0'&&p[i+5]<='9')) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "    if (!(p[i+7]>='0'&&p[i+7]<='9'&&p[i+8]>='0'&&p[i+8]<='9')) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "    if (p[i+3] != ':' || p[i+6] != ':') ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "    hour = (p[i+1]-'0')*10+(p[i+2]-'0');",
    "    minute = (p[i+4]-'0')*10+(p[i+5]-'0');",
    "    second = (p[i+7]-'0')*10+(p[i+8]-'0');",
    "    i += 9;",
    "    if (i < len && p[i] == '.') {",
    "      if (i + 3 >= len) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "      if (!(p[i+1]>='0'&&p[i+1]<='9'&&p[i+2]>='0'&&p[i+2]<='9'&&p[i+3]>='0'&&p[i+3]<='9')) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "      ms = (p[i+1]-'0')*100+(p[i+2]-'0')*10+(p[i+3]-'0');",
    "      i += 4;",
    "    }",
    "  }",
    "  int offset_min = 0;",
    "  if (i < len) {",
    "    char sign = p[i];",
    "    if (sign == 'Z') {",
    "      if (i + 1 != len) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "    } else if (sign == '+' || sign == '-') {",
    "      if (i + 6 != len) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "      if (!(p[i+1]>='0'&&p[i+1]<='9'&&p[i+2]>='0'&&p[i+2]<='9')) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "      if (!(p[i+4]>='0'&&p[i+4]<='9'&&p[i+5]>='0'&&p[i+5]<='9')) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "      if (p[i+3] != ':') ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "      int oh = (p[i+1]-'0')*10+(p[i+2]-'0');",
    "      int om = (p[i+4]-'0')*10+(p[i+5]-'0');",
    "      if (oh > 23 || om > 59) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "      offset_min = oh * 60 + om;",
    "      if (sign == '-') offset_min = -offset_min;",
    "    } else {",
    "      ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "    }",
    "  }",
    "  if (month < 1 || month > 12 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59 || ms < 0 || ms > 999) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "  int dim = ps_days_in_month(year, month);",
    "  if (day < 1 || day > dim) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "  __int128 sec = (__int128)ps_epoch_seconds_utc(year, month, day, hour, minute, second);",
    "  __int128 total = sec * 1000 + ms;",
    "  total -= (__int128)offset_min * 60 * 1000;",
    "  if (total < INT64_MIN || total > INT64_MAX) ps_raise_typed(\"InvalidISOFormatException\", ps_cstr(\"invalid ISO 8601 format\"));",
    "  return (int64_t)total;",
    "}",
    "static ps_string TimeCivil_formatISO8601(int64_t epoch_ms) {",
    "  time_t sec; int64_t ms;",
    "  ps_split_epoch_ms(epoch_ms, &sec, &ms);",
    "  struct tm tm;",
    "  if (!gmtime_r(&sec, &tm)) ps_raise_typed(\"InvalidDateException\", ps_cstr(\"invalid epoch\"));",
    "  int year = tm.tm_year + 1900;",
    "  int month = tm.tm_mon + 1;",
    "  int day = tm.tm_mday;",
    "  int hour = tm.tm_hour;",
    "  int minute = tm.tm_min;",
    "  int second = tm.tm_sec;",
    "  char ybuf[32];",
    "  int yabs = year < 0 ? -year : year;",
    "  if (yabs < 10) snprintf(ybuf, sizeof(ybuf), \"%s000%d\", year < 0 ? \"-\" : \"\", yabs);",
    "  else if (yabs < 100) snprintf(ybuf, sizeof(ybuf), \"%s00%d\", year < 0 ? \"-\" : \"\", yabs);",
    "  else if (yabs < 1000) snprintf(ybuf, sizeof(ybuf), \"%s0%d\", year < 0 ? \"-\" : \"\", yabs);",
    "  else snprintf(ybuf, sizeof(ybuf), \"%s%d\", year < 0 ? \"-\" : \"\", yabs);",
    "  char buf[64];",
    "  snprintf(buf, sizeof(buf), \"%s-%02d-%02dT%02d:%02d:%02d.%03lldZ\", ybuf, month, day, hour, minute, second, (long long)ms);",
    "  char* out = strdup(buf);",
    "  if (!out) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  return ps_string_from_owned(out);",
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
    "  char tmp[64];",
    "  int n = snprintf(tmp, sizeof(tmp), \"%.17g\", v);",
    "  if (n < 0) n = 0;",
    "  if (isfinite(v)) {",
    "    char *exp = strchr(tmp, 'e');",
    "    if (!exp) exp = strchr(tmp, 'E');",
    "    if (!exp) {",
    "      if (strchr(tmp, '.')) {",
    "        size_t len = strlen(tmp);",
    "        size_t best = len;",
    "        for (size_t cut = len; cut > 0; cut--) {",
    "          if (tmp[cut - 1] == '.') continue;",
    "          char cand[64];",
    "          memcpy(cand, tmp, cut);",
    "          cand[cut] = 0;",
    "          char *end = NULL;",
    "          double parsed = strtod(cand, &end);",
    "          if (end && *end == 0 && parsed == v) {",
    "            best = cut;",
    "            continue;",
    "          }",
    "          break;",
    "        }",
    "        tmp[best] = 0;",
    "        n = (int)best;",
    "      }",
    "    } else {",
    "      size_t mant_len = (size_t)(exp - tmp);",
    "      size_t best = mant_len;",
    "      for (size_t cut = mant_len; cut > 0; cut--) {",
    "        if (tmp[cut - 1] == '.') continue;",
    "        char cand[64];",
    "        size_t exp_len = strlen(exp);",
    "        if (cut + exp_len >= sizeof(cand)) continue;",
    "        memcpy(cand, tmp, cut);",
    "        memcpy(cand + cut, exp, exp_len + 1);",
    "        char *end = NULL;",
    "        double parsed = strtod(cand, &end);",
    "        if (end && *end == 0 && parsed == v) {",
    "          best = cut;",
    "          continue;",
    "        }",
    "        break;",
    "      }",
    "      if (best != mant_len) {",
    "        size_t exp_len = strlen(exp);",
    "        memmove(tmp + best, exp, exp_len + 1);",
    "        n = (int)(best + exp_len);",
    "      }",
    "    }",
    "  }",
    "  if (n >= (int)sizeof(buf[slot])) n = (int)sizeof(buf[slot]) - 1;",
    "  memcpy(buf[slot], tmp, (size_t)n);",
    "  buf[slot][n] = 0;",
    "  ps_string s = { buf[slot], (size_t)n };",
    "  return s;",
    "}",
    "",
    "static int ps_fs_invalid_path(int err) {",
    "  return err == ENAMETOOLONG || err == EINVAL || err == ELOOP;",
    "}",
    "static void ps_fs_throw_stat(int err) {",
    "  if (err == ENOENT) ps_raise_runtime_typed(\"FileNotFoundException\", \"file not found\");",
    "  if (err == EACCES || err == EPERM) ps_raise_runtime_typed(\"PermissionDeniedException\", \"permission denied\");",
    "  if (err == ENOTDIR || ps_fs_invalid_path(err)) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "}",
    "static void ps_fs_throw_open_dir(int err) {",
    "  if (err == ENOENT) ps_raise_runtime_typed(\"FileNotFoundException\", \"file not found\");",
    "  if (err == ENOTDIR) ps_raise_runtime_typed(\"NotADirectoryException\", \"not a directory\");",
    "  if (err == EACCES || err == EPERM) ps_raise_runtime_typed(\"PermissionDeniedException\", \"permission denied\");",
    "  if (ps_fs_invalid_path(err)) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "}",
    "static void ps_fs_throw_dir_op(int err) {",
    "  if (err == ENOENT) ps_raise_runtime_typed(\"FileNotFoundException\", \"file not found\");",
    "  if (err == ENOTDIR) ps_raise_runtime_typed(\"NotADirectoryException\", \"not a directory\");",
    "  if (err == ENOTEMPTY || err == EEXIST) ps_raise_runtime_typed(\"DirectoryNotEmptyException\", \"directory not empty\");",
    "  if (err == EACCES || err == EPERM) ps_raise_runtime_typed(\"PermissionDeniedException\", \"permission denied\");",
    "  if (ps_fs_invalid_path(err)) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "}",
    "static void ps_fs_throw_file_op(int err) {",
    "  if (err == ENOENT) ps_raise_runtime_typed(\"FileNotFoundException\", \"file not found\");",
    "  if (err == EISDIR) ps_raise_runtime_typed(\"NotAFileException\", \"not a file\");",
    "  if (err == ENOTDIR) ps_raise_runtime_typed(\"NotADirectoryException\", \"not a directory\");",
    "  if (err == EACCES || err == EPERM) ps_raise_runtime_typed(\"PermissionDeniedException\", \"permission denied\");",
    "  if (ps_fs_invalid_path(err)) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "}",
    "static void ps_fs_throw_common(int err) {",
    "  if (err == ENOENT) ps_raise_runtime_typed(\"FileNotFoundException\", \"file not found\");",
    "  if (err == EACCES || err == EPERM) ps_raise_runtime_typed(\"PermissionDeniedException\", \"permission denied\");",
    "  if (err == ENOTDIR || ps_fs_invalid_path(err)) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "}",
    "static ps_string ps_string_from_range(const char* s, size_t len) {",
    "  char* out = (char*)malloc(len + 1);",
    "  if (!out) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  if (len > 0) memcpy(out, s, len);",
    "  out[len] = 0;",
    "  return ps_string_from_owned(out);",
    "}",
    "static char* fs_join(const char* base, const char* name) {",
    "  if (!base || !name) return NULL;",
    "  size_t base_len = strlen(base);",
    "  size_t name_len = strlen(name);",
    "  int need_sep = 1;",
    "  if (base_len > 0 && base[base_len - 1] == '/') need_sep = 0;",
    "  size_t out_len = base_len + (need_sep ? 1 : 0) + name_len;",
    "  char* out = (char*)malloc(out_len + 1);",
    "  if (!out) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  memcpy(out, base, base_len);",
    "  size_t off = base_len;",
    "  if (need_sep) out[off++] = '/';",
    "  memcpy(out + off, name, name_len);",
    "  out[out_len] = 0;",
    "  return out;",
    "}",
    "static bool Fs_exists(ps_string path) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  struct stat st;",
    "  int ok = lstat(cpath, &st) == 0;",
    "  int err = errno;",
    "  free(cpath);",
    "  if (ok) return true;",
    "  if (err == ENOENT) return false;",
    "  if (err == ENOTDIR || ps_fs_invalid_path(err)) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  return false;",
    "}",
    "static bool Fs_isFile(ps_string path) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  struct stat st;",
    "  int ok = lstat(cpath, &st) == 0;",
    "  int err = errno;",
    "  free(cpath);",
    "  if (ok) return S_ISREG(st.st_mode);",
    "  if (err == ENOENT) return false;",
    "  if (err == ENOTDIR || ps_fs_invalid_path(err)) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  return false;",
    "}",
    "static bool Fs_isDir(ps_string path) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  struct stat st;",
    "  int ok = lstat(cpath, &st) == 0;",
    "  int err = errno;",
    "  free(cpath);",
    "  if (ok) return S_ISDIR(st.st_mode);",
    "  if (err == ENOENT) return false;",
    "  if (err == ENOTDIR || ps_fs_invalid_path(err)) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  return false;",
    "}",
    "static bool Fs_isSymlink(ps_string path) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  struct stat st;",
    "  int ok = lstat(cpath, &st) == 0;",
    "  int err = errno;",
    "  free(cpath);",
    "  if (ok) return S_ISLNK(st.st_mode);",
    "  if (err == ENOENT) return false;",
    "  if (err == ENOTDIR || ps_fs_invalid_path(err)) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  return false;",
    "}",
    "static bool Fs_isReadable(ps_string path) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  int ok = access(cpath, R_OK) == 0;",
    "  int err = errno;",
    "  free(cpath);",
    "  if (ok) return true;",
    "  if (err == ENOENT || err == EACCES || err == EPERM) return false;",
    "  if (err == ENOTDIR || ps_fs_invalid_path(err)) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  return false;",
    "}",
    "static bool Fs_isWritable(ps_string path) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  int ok = access(cpath, W_OK) == 0;",
    "  int err = errno;",
    "  free(cpath);",
    "  if (ok) return true;",
    "  if (err == ENOENT || err == EACCES || err == EPERM) return false;",
    "  if (err == ENOTDIR || ps_fs_invalid_path(err)) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  return false;",
    "}",
    "static bool Fs_isExecutable(ps_string path) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  int ok = access(cpath, X_OK) == 0;",
    "  int err = errno;",
    "  free(cpath);",
    "  if (ok) return true;",
    "  if (err == ENOENT || err == EACCES || err == EPERM) return false;",
    "  if (err == ENOTDIR || ps_fs_invalid_path(err)) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  return false;",
    "}",
    "static int64_t Fs_size(ps_string path) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  struct stat st;",
    "  if (stat(cpath, &st) != 0) {",
    "    int err = errno;",
    "    free(cpath);",
    "    ps_fs_throw_stat(err);",
    "  }",
    "  free(cpath);",
    "  if (!S_ISREG(st.st_mode)) ps_raise_runtime_typed(\"NotAFileException\", \"not a file\");",
    "  return (int64_t)st.st_size;",
    "}",
    "static void Fs_mkdir(ps_string path) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  if (mkdir(cpath, 0777) != 0) {",
    "    int err = errno;",
    "    free(cpath);",
    "    if (err == EEXIST) ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "    if (err == ENOTDIR) ps_raise_runtime_typed(\"NotADirectoryException\", \"not a directory\");",
    "    if (err == ENOENT) ps_raise_runtime_typed(\"FileNotFoundException\", \"file not found\");",
    "    if (err == EACCES || err == EPERM) ps_raise_runtime_typed(\"PermissionDeniedException\", \"permission denied\");",
    "    if (ps_fs_invalid_path(err)) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "    ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  }",
    "  free(cpath);",
    "}",
    "static void Fs_rmdir(ps_string path) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  if (rmdir(cpath) != 0) {",
    "    int err = errno;",
    "    free(cpath);",
    "    ps_fs_throw_dir_op(err);",
    "  }",
    "  free(cpath);",
    "}",
    "static void Fs_rm(ps_string path) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  if (unlink(cpath) != 0) {",
    "    int err = errno;",
    "    free(cpath);",
    "    ps_fs_throw_file_op(err);",
    "  }",
    "  free(cpath);",
    "}",
    "static void Fs_cp(ps_string src, ps_string dst) {",
    "  if (src.len == 0 || dst.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* csrc = ps_string_to_cstr(src);",
    "  char* cdst = ps_string_to_cstr(dst);",
    "  struct stat st;",
    "  if (stat(csrc, &st) != 0) {",
    "    int err = errno;",
    "    free(csrc);",
    "    free(cdst);",
    "    ps_fs_throw_stat(err);",
    "  }",
    "  if (!S_ISREG(st.st_mode)) {",
    "    free(csrc);",
    "    free(cdst);",
    "    ps_raise_runtime_typed(\"NotAFileException\", \"not a file\");",
    "  }",
    "  const char* slash = strrchr(cdst, '/');",
    "  size_t dir_len = slash ? (size_t)(slash - cdst) : 0;",
    "  char tmp_name[128];",
    "  char* tmp_path = NULL;",
    "  int tmp_fd = -1;",
    "  for (int attempt = 0; attempt < 16; attempt += 1) {",
    "    snprintf(tmp_name, sizeof(tmp_name), \".ps_tmp_%d_%d\", (int)getpid(), attempt);",
    "    if (slash) {",
    "      size_t tlen = dir_len + 1 + strlen(tmp_name);",
    "      tmp_path = (char*)malloc(tlen + 1);",
    "      if (!tmp_path) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "      memcpy(tmp_path, cdst, dir_len);",
    "      tmp_path[dir_len] = '/';",
    "      strcpy(tmp_path + dir_len + 1, tmp_name);",
    "    } else {",
    "      tmp_path = strdup(tmp_name);",
    "      if (!tmp_path) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    }",
    "    tmp_fd = open(tmp_path, O_WRONLY | O_CREAT | O_EXCL, 0600);",
    "    if (tmp_fd >= 0) break;",
    "    int err = errno;",
    "    free(tmp_path);",
    "    tmp_path = NULL;",
    "    if (err == EEXIST) continue;",
    "    free(csrc);",
    "    free(cdst);",
    "    ps_fs_throw_common(err);",
    "  }",
    "  if (tmp_fd < 0) {",
    "    free(csrc);",
    "    free(cdst);",
    "    ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  }",
    "  int src_fd = open(csrc, O_RDONLY);",
    "  if (src_fd < 0) {",
    "    int err = errno;",
    "    close(tmp_fd);",
    "    if (tmp_path) { unlink(tmp_path); free(tmp_path); }",
    "    free(csrc);",
    "    free(cdst);",
    "    ps_fs_throw_common(err);",
    "  }",
    "  uint8_t buf[16384];",
    "  int ok = 1;",
    "  while (1) {",
    "    ssize_t r = read(src_fd, buf, sizeof(buf));",
    "    if (r == 0) break;",
    "    if (r < 0) { ok = 0; break; }",
    "    size_t off = 0;",
    "    while (off < (size_t)r) {",
    "      ssize_t w = write(tmp_fd, buf + off, (size_t)r - off);",
    "      if (w <= 0) { ok = 0; break; }",
    "      off += (size_t)w;",
    "    }",
    "    if (!ok) break;",
    "  }",
    "  int write_err = errno;",
    "  close(src_fd);",
    "  close(tmp_fd);",
    "  if (!ok) {",
    "    if (tmp_path) { unlink(tmp_path); free(tmp_path); }",
    "    free(csrc);",
    "    free(cdst);",
    "    ps_fs_throw_common(write_err);",
    "  }",
    "  if (rename(tmp_path, cdst) != 0) {",
    "    int err = errno;",
    "    if (tmp_path) { unlink(tmp_path); free(tmp_path); }",
    "    free(csrc);",
    "    free(cdst);",
    "    ps_fs_throw_common(err);",
    "  }",
    "  free(tmp_path);",
    "  free(csrc);",
    "  free(cdst);",
    "}",
    "static void Fs_mv(ps_string src, ps_string dst) {",
    "  if (src.len == 0 || dst.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* csrc = ps_string_to_cstr(src);",
    "  char* cdst = ps_string_to_cstr(dst);",
    "  if (rename(csrc, cdst) != 0) {",
    "    int err = errno;",
    "    free(csrc);",
    "    free(cdst);",
    "    if (err == EXDEV) ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "    ps_fs_throw_common(err);",
    "  }",
    "  free(csrc);",
    "  free(cdst);",
    "}",
    "static void Fs_chmod(ps_string path, int64_t mode) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  if (chmod(cpath, (mode_t)mode) != 0) {",
    "    int err = errno;",
    "    free(cpath);",
    "    ps_fs_throw_common(err);",
    "  }",
    "  free(cpath);",
    "}",
    "static ps_string Fs_cwd(void) {",
    "  char* buf = getcwd(NULL, 0);",
    "  if (!buf) ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  return ps_string_from_owned(buf);",
    "}",
    "static void Fs_cd(ps_string path) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  if (chdir(cpath) != 0) {",
    "    int err = errno;",
    "    free(cpath);",
    "    if (err == ENOENT) ps_raise_runtime_typed(\"FileNotFoundException\", \"file not found\");",
    "    if (err == ENOTDIR) ps_raise_runtime_typed(\"NotADirectoryException\", \"not a directory\");",
    "    if (err == EACCES || err == EPERM) ps_raise_runtime_typed(\"PermissionDeniedException\", \"permission denied\");",
    "    if (ps_fs_invalid_path(err)) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "    ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  }",
    "  free(cpath);",
    "}",
    "static PathInfo* Fs_pathInfo(ps_string path) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  const char* ptr = path.ptr ? path.ptr : \"\";",
    "  size_t len = path.len;",
    "  const char* slash = NULL;",
    "  for (size_t i = 0; i < len; i += 1) {",
    "    if (ptr[i] == '/') slash = ptr + i;",
    "  }",
    "  size_t dirname_len = 0;",
    "  size_t basename_off = 0;",
    "  if (slash) {",
    "    size_t pos = (size_t)(slash - ptr);",
    "    dirname_len = pos == 0 ? 1 : pos;",
    "    basename_off = pos + 1;",
    "  }",
    "  size_t basename_len = len - basename_off;",
    "  const char* basename_ptr = ptr + basename_off;",
    "  size_t dot_pos = (size_t)-1;",
    "  for (size_t i = 0; i < basename_len; i += 1) {",
    "    if (basename_ptr[i] == '.') dot_pos = i;",
    "  }",
    "  size_t filename_len = basename_len;",
    "  size_t ext_len = 0;",
    "  if (dot_pos != (size_t)-1 && dot_pos > 0 && dot_pos + 1 < basename_len) {",
    "    filename_len = dot_pos;",
    "    ext_len = basename_len - dot_pos - 1;",
    "  }",
    "  PathInfo* pi = (PathInfo*)calloc(1, sizeof(PathInfo));",
    "  if (!pi) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  pi->dirname = ps_string_from_range(ptr, dirname_len);",
    "  pi->basename = ps_string_from_range(basename_ptr, basename_len);",
    "  pi->filename = ps_string_from_range(basename_ptr, filename_len);",
    "  pi->extension = ps_string_from_range(basename_ptr + filename_len + (ext_len ? 1 : 0), ext_len);",
    "  return pi;",
    "}",
    "static ps_fs_dir* Fs_openDir(ps_string path) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  DIR* dir = opendir(cpath);",
    "  if (!dir) {",
    "    int err = errno;",
    "    free(cpath);",
    "    ps_fs_throw_open_dir(err);",
    "  }",
    "  ps_fs_dir* d = (ps_fs_dir*)calloc(1, sizeof(ps_fs_dir));",
    "  if (!d) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  d->dir = dir;",
    "  d->path = cpath;",
    "  d->next = NULL;",
    "  d->done = 0;",
    "  d->closed = 0;",
    "  return d;",
    "}",
    "static bool Fs___dir_hasNext(ps_fs_dir* d) {",
    "  if (!d || d->closed || !d->dir) ps_raise_runtime_typed(\"IOException\", \"dir closed\");",
    "  if (d->next) return true;",
    "  if (d->done) return false;",
    "  errno = 0;",
    "  while (1) {",
    "    struct dirent* ent = readdir(d->dir);",
    "    if (!ent) break;",
    "    if (strcmp(ent->d_name, \".\") == 0 || strcmp(ent->d_name, \"..\") == 0) continue;",
    "    d->next = strdup(ent->d_name);",
    "    if (!d->next) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    return true;",
    "  }",
    "  if (errno != 0) ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  d->done = 1;",
    "  return false;",
    "}",
    "static ps_string Fs___dir_next(ps_fs_dir* d) {",
    "  if (!d) ps_raise_runtime_typed(\"IOException\", \"invalid dir\");",
    "  if (!Fs___dir_hasNext(d)) ps_raise_runtime_typed(\"IOException\", \"no more entries\");",
    "  size_t len = strlen(d->next);",
    "  ps_string s = ps_string_from_range(d->next, len);",
    "  free(d->next);",
    "  d->next = NULL;",
    "  return s;",
    "}",
    "static void Fs___dir_close(ps_fs_dir* d) {",
    "  if (!d) ps_raise_runtime_typed(\"IOException\", \"invalid dir\");",
    "  if (!d->closed) {",
    "    if (d->dir) closedir(d->dir);",
    "    d->dir = NULL;",
    "    d->closed = 1;",
    "    if (d->next) { free(d->next); d->next = NULL; }",
    "  }",
    "}",
    "static void Fs___dir_reset(ps_fs_dir* d) {",
    "  if (!d || d->closed || !d->dir) ps_raise_runtime_typed(\"IOException\", \"dir closed\");",
    "  if (d->next) { free(d->next); d->next = NULL; }",
    "  d->done = 0;",
    "  rewinddir(d->dir);",
    "}",
    "static int ps_fs_walker_push(ps_fs_walker* w, DIR* dir, char* path, int depth) {",
    "  if (w->len >= w->cap) {",
    "    size_t new_cap = w->cap == 0 ? 4 : w->cap * 2;",
    "    ps_fs_walk_frame* n = (ps_fs_walk_frame*)realloc(w->stack, sizeof(ps_fs_walk_frame) * new_cap);",
    "    if (!n) return 0;",
    "    w->stack = n;",
    "    w->cap = new_cap;",
    "  }",
    "  w->stack[w->len].dir = dir;",
    "  w->stack[w->len].path = path;",
    "  w->stack[w->len].depth = depth;",
    "  w->len += 1;",
    "  return 1;",
    "}",
    "static void ps_fs_walker_clear_next(ps_fs_walker* w) {",
    "  if (w->next.path) { free(w->next.path); w->next.path = NULL; }",
    "  if (w->next.name) { free(w->next.name); w->next.name = NULL; }",
    "  w->has_next = 0;",
    "}",
    "static void ps_fs_walker_close_all(ps_fs_walker* w) {",
    "  for (size_t i = 0; i < w->len; i += 1) {",
    "    if (w->stack[i].dir) closedir(w->stack[i].dir);",
    "    if (w->stack[i].path) free(w->stack[i].path);",
    "  }",
    "  free(w->stack);",
    "  w->stack = NULL;",
    "  w->len = 0;",
    "  w->cap = 0;",
    "  ps_fs_walker_clear_next(w);",
    "}",
    "static bool Fs___walker_hasNext(ps_fs_walker* w) {",
    "  if (!w || w->closed) ps_raise_runtime_typed(\"IOException\", \"walker closed\");",
    "  if (w->has_next) return true;",
    "  while (w->len > 0) {",
    "    ps_fs_walk_frame* fr = &w->stack[w->len - 1];",
    "    errno = 0;",
    "    struct dirent* ent = readdir(fr->dir);",
    "    if (!ent) {",
    "      if (errno != 0) ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "      closedir(fr->dir);",
    "      free(fr->path);",
    "      w->len -= 1;",
    "      continue;",
    "    }",
    "    if (strcmp(ent->d_name, \".\") == 0 || strcmp(ent->d_name, \"..\") == 0) continue;",
    "    char* full = fs_join(fr->path, ent->d_name);",
    "    if (!full) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    struct stat st;",
    "    if (lstat(full, &st) != 0) {",
    "      int err = errno;",
    "      free(full);",
    "      ps_fs_throw_stat(err);",
    "    }",
    "    int is_symlink = S_ISLNK(st.st_mode);",
    "    int is_dir = S_ISDIR(st.st_mode);",
    "    int is_file = S_ISREG(st.st_mode);",
    "    if (is_symlink && w->follow_symlinks) {",
    "      struct stat st2;",
    "      if (stat(full, &st2) != 0) {",
    "        int err = errno;",
    "        if (err != ENOENT) {",
    "          free(full);",
    "          ps_fs_throw_stat(err);",
    "        }",
    "        is_dir = 0;",
    "        is_file = 0;",
    "      } else {",
    "        is_dir = S_ISDIR(st2.st_mode);",
    "        is_file = S_ISREG(st2.st_mode);",
    "      }",
    "    } else if (is_symlink) {",
    "      is_dir = 0;",
    "      is_file = 0;",
    "    }",
    "    int depth = fr->depth;",
    "    if (is_dir && (w->max_depth < 0 || depth < w->max_depth)) {",
    "      DIR* child = opendir(full);",
    "      if (!child) {",
    "        int err = errno;",
    "        free(full);",
    "        ps_fs_throw_open_dir(err);",
    "      }",
    "      char* child_path = strdup(full);",
    "      if (!child_path || !ps_fs_walker_push(w, child, child_path, depth + 1)) {",
    "        if (child_path) free(child_path);",
    "        closedir(child);",
    "        free(full);",
    "        ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "      }",
    "    }",
    "    w->next.path = full;",
    "    w->next.name = strdup(ent->d_name);",
    "    if (!w->next.name) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    w->next.depth = depth;",
    "    w->next.is_dir = is_dir;",
    "    w->next.is_file = is_file;",
    "    w->next.is_symlink = is_symlink;",
    "    w->has_next = 1;",
    "    return true;",
    "  }",
    "  return false;",
    "}",
    "static PathEntry* Fs___walker_next(ps_fs_walker* w) {",
    "  if (!w) ps_raise_runtime_typed(\"IOException\", \"invalid walker\");",
    "  if (!Fs___walker_hasNext(w)) ps_raise_runtime_typed(\"IOException\", \"no more entries\");",
    "  PathEntry* e = (PathEntry*)calloc(1, sizeof(PathEntry));",
    "  if (!e) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  e->path = ps_string_from_range(w->next.path, strlen(w->next.path));",
    "  e->name = ps_string_from_range(w->next.name, strlen(w->next.name));",
    "  e->depth = (int64_t)w->next.depth;",
    "  e->isDir = w->next.is_dir ? true : false;",
    "  e->isFile = w->next.is_file ? true : false;",
    "  e->isSymlink = w->next.is_symlink ? true : false;",
    "  ps_fs_walker_clear_next(w);",
    "  return e;",
    "}",
    "static void Fs___walker_close(ps_fs_walker* w) {",
    "  if (!w) ps_raise_runtime_typed(\"IOException\", \"invalid walker\");",
    "  if (!w->closed) {",
    "    ps_fs_walker_close_all(w);",
    "    w->closed = 1;",
    "  }",
    "}",
    "static ps_fs_walker* Fs_walk(ps_string path, int64_t max_depth, bool follow_symlinks) {",
    "  if (path.len == 0) ps_raise_runtime_typed(\"InvalidPathException\", \"invalid path\");",
    "  char* cpath = ps_string_to_cstr(path);",
    "  DIR* dir = opendir(cpath);",
    "  if (!dir) {",
    "    int err = errno;",
    "    free(cpath);",
    "    ps_fs_throw_open_dir(err);",
    "  }",
    "  ps_fs_walker* w = (ps_fs_walker*)calloc(1, sizeof(ps_fs_walker));",
    "  if (!w) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  w->stack = NULL;",
    "  w->len = 0;",
    "  w->cap = 0;",
    "  w->max_depth = (int)max_depth;",
    "  w->follow_symlinks = follow_symlinks ? 1 : 0;",
    "  w->closed = 0;",
    "  w->has_next = 0;",
    "  if (!ps_fs_walker_push(w, dir, cpath, 0)) {",
    "    closedir(dir);",
    "    free(cpath);",
    "    ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  }",
    "  return w;",
    "}",
    "static void ps_sys_throw_exec_errno(int err) {",
    "  if (err == ENOENT || err == ENOTDIR) ps_raise_runtime_typed(\"InvalidExecutableException\", \"invalid executable\");",
    "  if (err == EACCES) ps_raise_runtime_typed(\"ProcessPermissionException\", \"permission denied\");",
    "  ps_raise_runtime_typed(\"ProcessExecutionException\", \"execution failed\");",
    "}",
    "static void ps_sys_set_nonblocking(int fd) {",
    "  int flags = fcntl(fd, F_GETFL, 0);",
    "  if (flags < 0) ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "}",
    "static void ps_sys_set_cloexec(int fd) {",
    "  int flags = fcntl(fd, F_GETFD, 0);",
    "  if (flags < 0) ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0) ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "}",
    "static ps_list_byte ps_list_byte_from_buf(const uint8_t* buf, size_t len) {",
    "  ps_list_byte out = { NULL, 0, 0, 0 };",
    "  if (len == 0) return out;",
    "  out.ptr = (uint8_t*)malloc(len);",
    "  if (!out.ptr) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  memcpy(out.ptr, buf, len);",
    "  out.len = len;",
    "  out.cap = len;",
    "  return out;",
    "}",
    "static void ps_list_ProcessEvent_push(ps_list_ProcessEvent* l, ProcessEvent* v) {",
    "  if (l->len >= l->cap) {",
    "    size_t new_cap = l->cap == 0 ? 8 : l->cap * 2;",
    "    ProcessEvent** n = (ProcessEvent**)realloc(l->ptr, sizeof(ProcessEvent*) * new_cap);",
    "    if (!n) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    l->ptr = n;",
    "    l->cap = new_cap;",
    "  }",
    "  l->ptr[l->len++] = v;",
    "  l->version += 1;",
    "}",
    "static ProcessEvent* ps_sys_make_event(int stream, const uint8_t* buf, size_t len) {",
    "  ProcessEvent* e = (ProcessEvent*)calloc(1, sizeof(ProcessEvent));",
    "  if (!e) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  e->stream = (int64_t)stream;",
    "  e->data = ps_list_byte_from_buf(buf, len);",
    "  return e;",
    "}",
    "static ProcessResult* Sys_execute(ps_string program, ps_list_string args, ps_list_byte input, bool capture_stdout, bool capture_stderr) {",
    "  if (program.len == 0) ps_raise_runtime_typed(\"InvalidExecutableException\", \"invalid executable\");",
    "  char* prog = ps_string_to_cstr(program);",
    "  size_t argc = args.len + 1;",
    "  char** argv = (char**)calloc(argc + 1, sizeof(char*));",
    "  if (!argv) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  argv[0] = prog;",
    "  for (size_t i = 0; i < args.len; i += 1) {",
    "    argv[i + 1] = ps_string_to_cstr(args.ptr[i]);",
    "  }",
    "  argv[argc] = NULL;",
    "  int in_pipe[2];",
    "  int out_pipe[2];",
    "  int err_pipe[2];",
    "  int exec_pipe[2];",
    "  if (pipe(in_pipe) != 0) ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  if (capture_stdout && pipe(out_pipe) != 0) ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  if (capture_stderr && pipe(err_pipe) != 0) ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  if (pipe(exec_pipe) != 0) ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  ps_sys_set_cloexec(exec_pipe[1]);",
    "  pid_t pid = fork();",
    "  if (pid < 0) ps_raise_runtime_typed(\"ProcessCreationException\", \"fork failed\");",
    "  if (pid == 0) {",
    "    close(exec_pipe[0]);",
    "    if (dup2(in_pipe[0], STDIN_FILENO) < 0) _exit(127);",
    "    close(in_pipe[0]);",
    "    close(in_pipe[1]);",
    "    if (capture_stdout) {",
    "      if (dup2(out_pipe[1], STDOUT_FILENO) < 0) _exit(127);",
    "      close(out_pipe[0]);",
    "      close(out_pipe[1]);",
    "    }",
    "    if (capture_stderr) {",
    "      if (dup2(err_pipe[1], STDERR_FILENO) < 0) _exit(127);",
    "      close(err_pipe[0]);",
    "      close(err_pipe[1]);",
    "    }",
    "    execve(prog, argv, environ);",
    "    int err = errno;",
    "    (void)write(exec_pipe[1], &err, sizeof(err));",
    "    _exit(127);",
    "  }",
    "  close(exec_pipe[1]);",
    "  close(in_pipe[0]);",
    "  if (capture_stdout) close(out_pipe[1]);",
    "  if (capture_stderr) close(err_pipe[1]);",
    "  int exec_err = 0;",
    "  ssize_t er = read(exec_pipe[0], &exec_err, sizeof(exec_err));",
    "  close(exec_pipe[0]);",
    "  if (er > 0) {",
    "    if (capture_stdout) close(out_pipe[0]);",
    "    if (capture_stderr) close(err_pipe[0]);",
    "    close(in_pipe[1]);",
    "    waitpid(pid, NULL, 0);",
    "    ps_sys_throw_exec_errno(exec_err);",
    "  }",
    "  ps_sys_set_nonblocking(in_pipe[1]);",
    "  if (capture_stdout) ps_sys_set_nonblocking(out_pipe[0]);",
    "  if (capture_stderr) ps_sys_set_nonblocking(err_pipe[0]);",
    "  size_t input_off = 0;",
    "  int stdin_open = input.len > 0 ? 1 : 0;",
    "  int stdout_open = capture_stdout ? 1 : 0;",
    "  int stderr_open = capture_stderr ? 1 : 0;",
    "  if (!stdin_open) { close(in_pipe[1]); }",
    "  ps_list_ProcessEvent events = { NULL, 0, 0, 0 };",
    "  uint8_t buf[4096];",
    "  while (stdin_open || stdout_open || stderr_open) {",
    "    struct pollfd fds[3];",
    "    nfds_t nfds = 0;",
    "    if (stdin_open && input_off < input.len) {",
    "      fds[nfds].fd = in_pipe[1];",
    "      fds[nfds].events = POLLOUT;",
    "      fds[nfds].revents = 0;",
    "      nfds++;",
    "    }",
    "    if (stdout_open) {",
    "      fds[nfds].fd = out_pipe[0];",
    "      fds[nfds].events = POLLIN;",
    "      fds[nfds].revents = 0;",
    "      nfds++;",
    "    }",
    "    if (stderr_open) {",
    "      fds[nfds].fd = err_pipe[0];",
    "      fds[nfds].events = POLLIN;",
    "      fds[nfds].revents = 0;",
    "      nfds++;",
    "    }",
    "    if (nfds == 0) break;",
    "    int pr = poll(fds, nfds, -1);",
    "    if (pr < 0) {",
    "      if (errno == EINTR) continue;",
    "      ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "    }",
    "    nfds_t idx = 0;",
    "    if (stdin_open && input_off < input.len) {",
    "      if (fds[idx].revents & POLLOUT) {",
    "        ssize_t w = write(in_pipe[1], input.ptr + input_off, input.len - input_off);",
    "        if (w > 0) {",
    "          input_off += (size_t)w;",
    "          if (input_off >= input.len) {",
    "            close(in_pipe[1]);",
    "            stdin_open = 0;",
    "          }",
    "        } else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {",
    "          close(in_pipe[1]);",
    "          stdin_open = 0;",
    "        }",
    "      }",
    "      idx++;",
    "    }",
    "    if (stdout_open) {",
    "      if (fds[idx].revents & (POLLIN | POLLHUP)) {",
    "        ssize_t r = read(out_pipe[0], buf, sizeof(buf));",
    "        if (r > 0) {",
    "          ProcessEvent* e = ps_sys_make_event(1, buf, (size_t)r);",
    "          ps_list_ProcessEvent_push(&events, e);",
    "        } else if (r == 0) {",
    "          close(out_pipe[0]);",
    "          stdout_open = 0;",
    "        }",
    "      }",
    "      idx++;",
    "    }",
    "    if (stderr_open) {",
    "      if (fds[idx].revents & (POLLIN | POLLHUP)) {",
    "        ssize_t r = read(err_pipe[0], buf, sizeof(buf));",
    "        if (r > 0) {",
    "          ProcessEvent* e = ps_sys_make_event(2, buf, (size_t)r);",
    "          ps_list_ProcessEvent_push(&events, e);",
    "        } else if (r == 0) {",
    "          close(err_pipe[0]);",
    "          stderr_open = 0;",
    "        }",
    "      }",
    "      idx++;",
    "    }",
    "  }",
    "  int status = 0;",
    "  if (waitpid(pid, &status, 0) < 0) ps_raise_runtime_typed(\"IOException\", \"io failed\");",
    "  int64_t exit_code = 0;",
    "  if (WIFEXITED(status)) exit_code = (int64_t)WEXITSTATUS(status);",
    "  else if (WIFSIGNALED(status)) exit_code = 128 + (int64_t)WTERMSIG(status);",
    "  ProcessResult* res = (ProcessResult*)calloc(1, sizeof(ProcessResult));",
    "  if (!res) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  res->exitCode = exit_code;",
    "  res->events = events;",
    "  for (size_t i = 0; i < argc; i += 1) {",
    "    if (argv[i]) free(argv[i]);",
    "  }",
    "  free(argv);",
    "  return res;",
    "}",
    "typedef struct {",
    "  RegExp* key;",
    "  regex_t re;",
    "  ps_string pattern;",
    "  ps_string flags;",
    "  int used;",
    "} ps_rx_slot;",
    "static ps_rx_slot* ps_rx_slots = NULL;",
    "static size_t ps_rx_len = 0;",
    "static size_t ps_rx_cap = 0;",
    "static ps_rx_slot* ps_rx_get(RegExp* key) {",
    "  for (size_t i = 0; i < ps_rx_len; i += 1) if (ps_rx_slots[i].used && ps_rx_slots[i].key == key) return &ps_rx_slots[i];",
    "  return NULL;",
    "}",
    "static ps_rx_slot* ps_rx_put(RegExp* key) {",
    "  if (ps_rx_len == ps_rx_cap) {",
    "    size_t nc = ps_rx_cap == 0 ? 8 : ps_rx_cap * 2;",
    "    ps_rx_slot* n = (ps_rx_slot*)realloc(ps_rx_slots, sizeof(ps_rx_slot) * nc);",
    "    if (!n) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    ps_rx_slots = n;",
    "    ps_rx_cap = nc;",
    "  }",
    "  ps_rx_slot* s = &ps_rx_slots[ps_rx_len++];",
    "  memset(s, 0, sizeof(*s));",
    "  s->used = 1;",
    "  s->key = key;",
    "  return s;",
    "}",
    "static ps_string ps_rx_copy(const char* p, size_t len) {",
    "  char* b = (char*)malloc(len + 1);",
    "  if (!b) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  if (len > 0) memcpy(b, p, len);",
    "  b[len] = '\\0';",
    "  ps_string s; s.ptr = b; s.len = len; return s;",
    "}",
    "static char* ps_rx_to_cstr(ps_string s) {",
    "  char* b = (char*)malloc(s.len + 1);",
    "  if (!b) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  if (s.len > 0 && s.ptr) memcpy(b, s.ptr, s.len);",
    "  b[s.len] = '\\0';",
    "  return b;",
    "}",
    "static void ps_rx_append(char** out, size_t* len, size_t* cap, const char* s) {",
    "  size_t n = strlen(s);",
    "  if (*len + n + 1 > *cap) {",
    "    while (*len + n + 1 > *cap) *cap *= 2;",
    "    char* p = (char*)realloc(*out, *cap);",
    "    if (!p) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    *out = p;",
    "  }",
    "  memcpy(*out + *len, s, n);",
    "  *len += n;",
    "  (*out)[*len] = '\\0';",
    "}",
    "static char* ps_rx_translate_pattern(ps_string pattern) {",
    "  size_t cap = pattern.len * 8 + 32;",
    "  char* out = (char*)malloc(cap);",
    "  if (!out) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  size_t w = 0;",
    "  out[0] = '\\0';",
    "  for (size_t i = 0; i < pattern.len; i += 1) {",
    "    char ch = pattern.ptr[i];",
    "    if (ch == '\\\\' && i + 1 < pattern.len) {",
    "      char nx = pattern.ptr[i + 1];",
    "      if (nx == 'w') { ps_rx_append(&out, &w, &cap, \"[[:alnum:]_]\"); i += 1; continue; }",
    "      if (nx == 'W') { ps_rx_append(&out, &w, &cap, \"[^[:alnum:]_]\"); i += 1; continue; }",
    "      if (nx == 'd') { ps_rx_append(&out, &w, &cap, \"[[:digit:]]\"); i += 1; continue; }",
    "      if (nx == 'D') { ps_rx_append(&out, &w, &cap, \"[^[:digit:]]\"); i += 1; continue; }",
    "      if (nx == 's') { ps_rx_append(&out, &w, &cap, \"[[:space:]]\"); i += 1; continue; }",
    "      if (nx == 'S') { ps_rx_append(&out, &w, &cap, \"[^[:space:]]\"); i += 1; continue; }",
    "    }",
    "    if (w + 2 > cap) {",
    "      cap *= 2;",
    "      char* p = (char*)realloc(out, cap);",
    "      if (!p) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "      out = p;",
    "    }",
    "    out[w++] = ch;",
    "    out[w] = '\\0';",
    "  }",
    "  return out;",
    "}",
    "static int ps_rx_utf8_next(const char* p, size_t len, size_t i, size_t* next) {",
    "  if (i >= len) return 0;",
    "  unsigned char c = (unsigned char)p[i];",
    "  if (c < 0x80) { *next = i + 1; return 1; }",
    "  if ((c & 0xE0) == 0xC0) { if (i + 1 >= len) return 0; *next = i + 2; return 1; }",
    "  if ((c & 0xF0) == 0xE0) { if (i + 2 >= len) return 0; *next = i + 3; return 1; }",
    "  if ((c & 0xF8) == 0xF0) { if (i + 3 >= len) return 0; *next = i + 4; return 1; }",
    "  return 0;",
    "}",
    "static size_t ps_rx_glyph_to_byte(ps_string s, int64_t glyph) {",
    "  if (glyph <= 0) return 0;",
    "  size_t i = 0, g = 0;",
    "  while (i < s.len && g < (size_t)glyph) {",
    "    size_t n = i;",
    "    if (!ps_rx_utf8_next(s.ptr, s.len, i, &n)) return i;",
    "    i = n;",
    "    g += 1;",
    "  }",
    "  return i;",
    "}",
    "static int64_t ps_rx_byte_to_glyph(ps_string s, size_t byte_pos) {",
    "  size_t i = 0, g = 0;",
    "  while (i < s.len && i < byte_pos) {",
    "    size_t n = i;",
    "    if (!ps_rx_utf8_next(s.ptr, s.len, i, &n)) break;",
    "    i = n;",
    "    g += 1;",
    "  }",
    "  return (int64_t)g;",
    "}",
    "static void ps_list_RegExpMatch_push(ps_list_RegExpMatch* l, RegExpMatch* v) {",
    "  if (l->len >= l->cap) {",
    "    size_t nc = l->cap == 0 ? 8 : l->cap * 2;",
    "    RegExpMatch** n = (RegExpMatch**)realloc(l->ptr, sizeof(RegExpMatch*) * nc);",
    "    if (!n) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "    l->ptr = n;",
    "    l->cap = nc;",
    "  }",
    "  l->ptr[l->len++] = v;",
    "  l->version += 1;",
    "}",
    "static void ps_rx_fail(const char* kind, const char* msg) {",
    "  char b[512];",
    "  if (!kind) kind = \"RegExpError\";",
    "  if (!msg) msg = \"error\";",
    "  snprintf(b, sizeof(b), \"%s: %s\", kind, msg);",
    "  ps_panic(\"R1010\", \"RUNTIME_MODULE_ERROR\", b);",
    "}",
    "static RegExpMatch* ps_rx_match_empty(void) {",
    "  RegExpMatch* m = (RegExpMatch*)calloc(1, sizeof(RegExpMatch));",
    "  if (!m) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  m->ok = false;",
    "  m->start = 0;",
    "  m->end = 0;",
    "  m->groups.ptr = NULL; m->groups.len = 0; m->groups.cap = 0; m->groups.version = 0;",
    "  return m;",
    "}",
    "static RegExp* RegExp_compile(ps_string pattern, ps_string flags) {",
    "  char* p = ps_rx_to_cstr(pattern);",
    "  char* f = ps_rx_to_cstr(flags);",
    "  if (strstr(p, \"(?=\") || strstr(p, \"(?!\") || strstr(p, \"(?<=\") || strstr(p, \"(?<!\")) {",
    "    free(p); free(f);",
    "    ps_rx_fail(\"RegExpSyntax\", \"forbidden metasyntax (backreference/lookaround)\");",
    "  }",
    "  for (size_t i = 0; i + 1 < strlen(p); i += 1) {",
    "    if (p[i] == '\\\\' && p[i + 1] >= '1' && p[i + 1] <= '9') {",
    "      free(p); free(f);",
    "      ps_rx_fail(\"RegExpSyntax\", \"forbidden metasyntax (backreference/lookaround)\");",
    "    }",
    "  }",
    "  char* pre = ps_rx_translate_pattern(pattern);",
    "  int cflags = REG_EXTENDED;",
    "  int has_i = 0, has_m = 0, has_s = 0;",
    "  for (size_t i = 0; i < strlen(f); i += 1) {",
    "    if (f[i] == 'i') has_i = 1;",
    "    else if (f[i] == 'm') has_m = 1;",
    "    else if (f[i] == 's') has_s = 1;",
    "    else { free(p); free(pre); free(f); ps_rx_fail(\"RegExpSyntax\", \"unsupported flag\"); }",
    "  }",
    "  if (has_i) cflags |= REG_ICASE;",
    "#ifdef REG_NEWLINE",
    "  if (has_m) cflags |= REG_NEWLINE;",
    "#endif",
    "  (void)has_s;",
    "  RegExp* r = (RegExp*)calloc(1, sizeof(RegExp));",
    "  if (!r) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  ps_rx_slot* slot = ps_rx_put(r);",
    "  slot->pattern = pattern;",
    "  char ff[4]; size_t k = 0;",
    "  if (has_i) ff[k++] = 'i'; if (has_m) ff[k++] = 'm'; if (has_s) ff[k++] = 's'; ff[k] = '\\0';",
    "  slot->flags = ps_rx_copy(ff, k);",
    "  int rc = regcomp(&slot->re, pre, cflags);",
    "  free(pre); free(p); free(f);",
    "  if (rc != 0) {",
    "    char eb[256]; regerror(rc, &slot->re, eb, sizeof(eb));",
    "    ps_rx_fail(\"RegExpSyntax\", eb);",
    "  }",
    "  return r;",
    "}",
    "static RegExpMatch* RegExp_find(RegExp* self, ps_string input, int64_t start) {",
    "  if (!self) ps_rx_fail(\"RegExpRange\", \"invalid RegExp handle\");",
    "  ps_rx_slot* slot = ps_rx_get(self);",
    "  if (!slot) ps_rx_fail(\"RegExpRange\", \"invalid RegExp handle\");",
    "  size_t gl = ps_utf8_glyph_len(input);",
    "  if (start < 0 || (size_t)start > gl) ps_rx_fail(\"RegExpRange\", \"start out of range\");",
    "  size_t sb = ps_rx_glyph_to_byte(input, start);",
    "  char* c = ps_rx_to_cstr(input);",
    "  size_t nmatch = slot->re.re_nsub + 1;",
    "  regmatch_t* pm = (regmatch_t*)calloc(nmatch, sizeof(regmatch_t));",
    "  if (!pm) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  int rc = regexec(&slot->re, c + sb, nmatch, pm, 0);",
    "  if (rc == REG_NOMATCH) { free(pm); free(c); return ps_rx_match_empty(); }",
    "  if (rc != 0) { free(pm); free(c); ps_rx_fail(\"RegExpLimit\", \"regex exec failed\"); }",
    "  RegExpMatch* m = (RegExpMatch*)calloc(1, sizeof(RegExpMatch));",
    "  if (!m) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  m->ok = true;",
    "  size_t bs = sb + (size_t)pm[0].rm_so;",
    "  size_t be = sb + (size_t)pm[0].rm_eo;",
    "  m->start = ps_rx_byte_to_glyph(input, bs);",
    "  m->end = ps_rx_byte_to_glyph(input, be);",
    "  m->groups.ptr = NULL; m->groups.len = 0; m->groups.cap = 0; m->groups.version = 0;",
    "  for (size_t i = 0; i < nmatch; i += 1) {",
    "    ps_string gs;",
    "    if (pm[i].rm_so < 0 || pm[i].rm_eo < 0) gs = ps_cstr(\"\");",
    "    else {",
    "      size_t a = sb + (size_t)pm[i].rm_so;",
    "      size_t b = sb + (size_t)pm[i].rm_eo;",
      "      gs = ps_rx_copy(input.ptr + a, b - a);",
    "    }",
    "    ps_list_string_push(&m->groups, gs);",
    "  }",
    "  free(pm); free(c);",
    "  return m;",
    "}",
    "static bool RegExp_test(RegExp* self, ps_string input, int64_t start) {",
    "  RegExpMatch* m = RegExp_find(self, input, start);",
    "  return m->ok;",
    "}",
    "static ps_string ps_rx_expand(ps_string repl, ps_list_string groups) {",
    "  size_t cap = repl.len + 32;",
    "  char* buf = (char*)malloc(cap);",
    "  if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  size_t w = 0;",
    "  for (size_t i = 0; i < repl.len; i += 1) {",
    "    char ch = repl.ptr[i];",
    "    if (ch != '$') {",
    "      if (w + 1 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\"); }",
    "      buf[w++] = ch;",
    "      continue;",
    "    }",
    "    if (i + 1 < repl.len && repl.ptr[i + 1] == '$') {",
    "      if (w + 1 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\"); }",
    "      buf[w++] = '$'; i += 1; continue;",
    "    }",
    "    if (i + 1 >= repl.len || !isdigit((unsigned char)repl.ptr[i + 1])) {",
    "      if (w + 1 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\"); }",
    "      buf[w++] = '$'; continue;",
    "    }",
    "    int idx = repl.ptr[i + 1] - '0'; i += 1;",
    "    if (i + 1 < repl.len && isdigit((unsigned char)repl.ptr[i + 1])) { idx = idx * 10 + (repl.ptr[i + 1] - '0'); i += 1; }",
    "    if (idx >= 0 && (size_t)idx < groups.len) {",
    "      ps_string g = groups.ptr[idx];",
    "      if (w + g.len > cap) { while (w + g.len > cap) cap *= 2; buf = (char*)realloc(buf, cap); if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\"); }",
    "      memcpy(buf + w, g.ptr, g.len); w += g.len;",
    "    }",
    "  }",
    "  return ps_rx_copy(buf, w);",
    "}",
    "static ps_string RegExp_replaceAll(RegExp* self, ps_string input, ps_string replacement, int64_t start, int64_t max) {",
    "  size_t gl = ps_utf8_glyph_len(input);",
    "  if (start < 0 || (size_t)start > gl) ps_rx_fail(\"RegExpRange\", \"start out of range\");",
    "  if (max < -1) ps_rx_fail(\"RegExpRange\", \"max out of range\");",
    "  if (max == 0) return input;",
    "  size_t cap = input.len + replacement.len + 64;",
    "  char* buf = (char*)malloc(cap);",
    "  if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\");",
    "  size_t w = 0;",
    "  size_t cur_g = (size_t)start;",
    "  size_t cur_b = ps_rx_glyph_to_byte(input, start);",
    "  memcpy(buf + w, input.ptr, cur_b); w += cur_b;",
    "  int64_t done = 0;",
    "  while (cur_g <= gl) {",
    "    RegExpMatch* m = RegExp_find(self, input, (int64_t)cur_g);",
    "    if (!m->ok) break;",
    "    size_t ms = ps_rx_glyph_to_byte(input, m->start);",
    "    size_t me = ps_rx_glyph_to_byte(input, m->end);",
    "    if (w + (ms - cur_b) + 1 > cap) { while (w + (ms - cur_b) + 1 > cap) cap *= 2; buf = (char*)realloc(buf, cap); if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\"); }",
    "    memcpy(buf + w, input.ptr + cur_b, ms - cur_b); w += (ms - cur_b);",
    "    ps_string exp = ps_rx_expand(replacement, m->groups);",
    "    if (w + exp.len + 1 > cap) { while (w + exp.len + 1 > cap) cap *= 2; buf = (char*)realloc(buf, cap); if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\"); }",
    "    memcpy(buf + w, exp.ptr, exp.len); w += exp.len;",
    "    done += 1;",
    "    if (max > 0 && done >= max) { cur_b = me; break; }",
    "    if (m->end <= m->start) {",
    "      if ((size_t)m->end >= gl) { cur_b = me; break; }",
    "      size_t next_g = (size_t)m->end + 1;",
    "      size_t next_b = ps_rx_glyph_to_byte(input, (int64_t)next_g);",
    "      if (w + (next_b - me) + 1 > cap) { while (w + (next_b - me) + 1 > cap) cap *= 2; buf = (char*)realloc(buf, cap); if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\"); }",
    "      memcpy(buf + w, input.ptr + me, next_b - me); w += (next_b - me);",
    "      cur_g = next_g;",
    "      cur_b = next_b;",
    "    } else {",
    "      cur_g = (size_t)m->end;",
    "      cur_b = me;",
    "    }",
    "  }",
    "  if (cur_b < input.len) {",
    "    if (w + (input.len - cur_b) + 1 > cap) { while (w + (input.len - cur_b) + 1 > cap) cap *= 2; buf = (char*)realloc(buf, cap); if (!buf) ps_panic(\"R1998\", \"RUNTIME_OOM\", \"out of memory\"); }",
    "    memcpy(buf + w, input.ptr + cur_b, input.len - cur_b); w += (input.len - cur_b);",
    "  }",
    "  return ps_rx_copy(buf, w);",
    "}",
    "static ps_string RegExp_replaceFirst(RegExp* self, ps_string input, ps_string replacement, int64_t start) {",
    "  return RegExp_replaceAll(self, input, replacement, start, 1);",
    "}",
    "static ps_list_RegExpMatch RegExp_findAll(RegExp* self, ps_string input, int64_t start, int64_t max) {",
    "  size_t gl = ps_utf8_glyph_len(input);",
    "  if (start < 0 || (size_t)start > gl) ps_rx_fail(\"RegExpRange\", \"start out of range\");",
    "  if (max < -1) ps_rx_fail(\"RegExpRange\", \"max out of range\");",
    "  ps_list_RegExpMatch out = { NULL, 0, 0, 0 };",
    "  if (max == 0) return out;",
    "  size_t cur = (size_t)start;",
    "  while (cur <= gl) {",
    "    RegExpMatch* m = RegExp_find(self, input, (int64_t)cur);",
    "    if (!m->ok) break;",
    "    ps_list_RegExpMatch_push(&out, m);",
    "    if (max > 0 && (int64_t)out.len >= max) break;",
    "    if (m->end <= m->start) {",
    "      if ((size_t)m->end >= gl) break;",
    "      cur = (size_t)m->end + 1;",
    "    } else cur = (size_t)m->end;",
    "  }",
    "  return out;",
    "}",
    "static ps_list_string RegExp_split(RegExp* self, ps_string input, int64_t start, int64_t maxParts) {",
    "  size_t gl = ps_utf8_glyph_len(input);",
    "  if (start < 0 || (size_t)start > gl) ps_rx_fail(\"RegExpRange\", \"start out of range\");",
    "  if (maxParts < 0) ps_rx_fail(\"RegExpRange\", \"maxParts out of range\");",
    "  ps_list_string out = { NULL, 0, 0, 0 };",
    "  if (maxParts == 0) return out;",
    "  size_t cur = (size_t)start;",
    "  size_t cur_b = ps_rx_glyph_to_byte(input, start);",
    "  if (maxParts == 1) { ps_list_string_push(&out, ps_rx_copy(input.ptr + cur_b, input.len - cur_b)); return out; }",
    "  while (cur <= gl && (int64_t)out.len + 1 < maxParts) {",
    "    RegExpMatch* m = RegExp_find(self, input, (int64_t)cur);",
    "    if (!m->ok) break;",
    "    size_t ms = ps_rx_glyph_to_byte(input, m->start);",
    "    size_t me = ps_rx_glyph_to_byte(input, m->end);",
    "    ps_list_string_push(&out, ps_rx_copy(input.ptr + cur_b, ms - cur_b));",
    "    if (m->end <= m->start) {",
    "      if ((size_t)m->end >= gl) { cur = (size_t)m->end; cur_b = me; break; }",
    "      cur = (size_t)m->end + 1; cur_b = ps_rx_glyph_to_byte(input, (int64_t)cur);",
    "    } else { cur = (size_t)m->end; cur_b = me; }",
    "  }",
    "  if ((int64_t)out.len < maxParts) ps_list_string_push(&out, ps_rx_copy(input.ptr + cur_b, input.len - cur_b));",
    "  return out;",
    "}",
    "static ps_string RegExp_pattern(RegExp* self) {",
    "  if (!self) ps_rx_fail(\"RegExpRange\", \"invalid RegExp handle\");",
    "  ps_rx_slot* s = ps_rx_get(self); if (!s) ps_rx_fail(\"RegExpRange\", \"invalid RegExp handle\");",
    "  return s->pattern;",
    "}",
    "static ps_string RegExp_flags(RegExp* self) {",
    "  if (!self) ps_rx_fail(\"RegExpRange\", \"invalid RegExp handle\");",
    "  ps_rx_slot* s = ps_rx_get(self); if (!s) ps_rx_fail(\"RegExpRange\", \"invalid RegExp handle\");",
    "  return s->flags;",
    "}",
  ];
}

function emitFunctionPrototypes(ir) {
  return ir.functions.map((fn) => {
    const ret = fn.name === "main" ? "int" : cTypeFromName(fn.returnType.name);
    const params = fn.params.map((p) => `${cTypeFromName(p.type.name)} ${cIdent(p.name)}`).join(", ");
    return `${ret} ${cIdent(cFunctionName(fn))}(${params || "void"});`;
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
      } else if (i.literalType === "TextFile" || i.literalType === "BinaryFile") {
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
      {
        const dt = t(i.name);
        const st = t(i.src);
        if (PROTO_MAP.has(dt) && isExceptionProto(PROTO_MAP, dt) && (st === "Exception" || st === "RuntimeException")) {
          out.push(`${n(i.name)} = (${dt}*)${n(i.src)};`);
        } else {
          out.push(`${n(i.name)} = ${n(i.src)};`);
        }
      }
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
      if (i.variadic && VARIADIC_PARAM_INFO.has(i.callee)) {
        const info = VARIADIC_PARAM_INFO.get(i.callee);
        const viewType = info.type;
        const fixedCount = info.fixedCount;
        const cont = parseContainer(viewType);
        const inner = cont?.inner || "int";
        const viewC = cTypeFromName(viewType);
        const innerC = cTypeFromName(inner);
        const tmp = `__va_${VARARG_COUNTER++}`;
        out.push(`{ ${viewC} ${tmp};`);
        out.push(`${tmp}.len = ${i.args.length - fixedCount};`);
        if (i.args.length > fixedCount) {
          const tmpData = `${tmp}_data`;
          out.push(`${innerC} ${tmpData}[] = { ${i.args.slice(fixedCount).map((a) => n(a)).join(", ")} };`);
          out.push(`${tmp}.ptr = ${tmpData};`);
        } else {
          out.push(`${tmp}.ptr = NULL;`);
        }
        out.push(`${tmp}.version_ptr = NULL;`);
        out.push(`${tmp}.version = 0;`);
        const fixedArgs = i.args.slice(0, fixedCount).map((a) => n(a));
        const callArgs = [...fixedArgs, tmp].join(", ");
        if (t(i.dst) === "void") out.push(`${cIdent(i.callee)}(${callArgs});`);
        else out.push(`${n(i.dst)} = ${cIdent(i.callee)}(${callArgs});`);
        out.push("}");
      } else if (i.callee === "Exception") {
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
        const arg = i.args[0];
        const at = t(arg);
        if (at && at !== "string") {
          if (at === "int") out.push(`${cIdent(i.callee)}(ps_i64_to_string(${n(arg)}));`);
          else if (at === "float") out.push(`${cIdent(i.callee)}(ps_f64_to_string(${n(arg)}));`);
          else if (at === "bool") out.push(`${cIdent(i.callee)}(${n(arg)} ? ps_cstr("true") : ps_cstr("false"));`);
          else if (at === "byte" || at === "glyph") out.push(`${cIdent(i.callee)}(ps_glyph_to_string((uint32_t)${n(arg)}));`);
          else out.push('ps_raise_runtime_typed("InvalidArgumentException", "invalid argument");');
        } else {
          out.push(`${cIdent(i.callee)}(${i.args.map(n).join(", ")});`);
        }
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
        if (rt === "TextFile" || rt === "BinaryFile") {
          if (i.method === "read") {
            if (i.args.length !== 1) {
              out.push('ps_raise_runtime_typed("InvalidArgumentException", "invalid read size");');
            } else if (rt === "BinaryFile") {
              out.push(`${n(i.dst)} = ps_file_read_size_bytes(${n(i.receiver)}, ${n(i.args[0])});`);
            } else {
              out.push(`${n(i.dst)} = ps_file_read_size_glyphs(${n(i.receiver)}, ${n(i.args[0])});`);
            }
          } else if (i.method === "write") {
            const at = t(i.args[0]);
            if (rt === "TextFile") {
              if (at === "string") out.push(`ps_file_write_text(${n(i.receiver)}, ${n(i.args[0])});`);
              else out.push('ps_raise_runtime_typed("InvalidArgumentException", "invalid write value");');
            } else {
              if (parseContainer(at)?.kind === "list" && parseContainer(at)?.inner === "byte") {
                out.push(`ps_file_write_bytes(${n(i.receiver)}, ${n(i.args[0])});`);
              } else {
                out.push('ps_raise_runtime_typed("InvalidArgumentException", "invalid write value");');
              }
            }
          } else if (i.method === "tell") {
            if (rt === "BinaryFile") out.push(`${n(i.dst)} = ps_file_tell_bytes(${n(i.receiver)});`);
            else out.push(`${n(i.dst)} = ps_file_tell_glyphs(${n(i.receiver)});`);
          } else if (i.method === "seek") {
            if (i.args.length !== 1) {
              out.push('ps_raise_runtime_typed("InvalidArgumentException", "invalid seek position");');
            } else if (rt === "BinaryFile") {
              out.push(`ps_file_seek_bytes(${n(i.receiver)}, ${n(i.args[0])});`);
            } else {
              out.push(`ps_file_seek_glyphs(${n(i.receiver)}, ${n(i.args[0])});`);
            }
          } else if (i.method === "size") {
            if (rt === "BinaryFile") out.push(`${n(i.dst)} = ps_file_size_bytes(${n(i.receiver)});`);
            else out.push(`${n(i.dst)} = ps_file_size_glyphs(${n(i.receiver)});`);
          } else if (i.method === "name") {
            out.push(`${n(i.dst)} = ps_file_name(${n(i.receiver)});`);
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
        } else if (rc && rc.kind === "map" && i.method === "remove") {
          const m = parseMapType(rt);
          if (m) {
            const recv = aliasOf(i.receiver) || i.receiver;
            out.push(`${n(i.dst)} = ps_map_remove_${m.base}(&${n(recv)}, ${n(i.args[0])});`);
          }
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
        } else if (rt === "int" && i.method === "toInt") {
          out.push(`${n(i.dst)} = ${n(i.receiver)};`);
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
        } else if (rc && rc.kind === "list" && i.method === "reverse") {
          const recv = aliasOf(i.receiver) || i.receiver;
          out.push(`{`);
          out.push(`  size_t n = ${n(recv)}.len;`);
          out.push(`  for (size_t i = 0; i < n / 2; i += 1) {`);
          out.push(`    ${cTypeFromName(rc.inner)} tmp = ${n(recv)}.ptr[i];`);
          out.push(`    ${n(recv)}.ptr[i] = ${n(recv)}.ptr[n - 1 - i];`);
          out.push(`    ${n(recv)}.ptr[n - 1 - i] = tmp;`);
          out.push(`  }`);
          out.push(`  ${n(i.dst)} = (int64_t)${n(recv)}.len;`);
          out.push(`}`);
        } else if (rc && rc.kind === "list" && i.method === "sort") {
          const recv = aliasOf(i.receiver) || i.receiver;
          const inner = rc.inner;
          out.push(`{`);
          out.push(`  size_t n = ${n(recv)}.len;`);
          out.push(`  if (n > 1) {`);
          out.push(`    ${cTypeFromName(inner)}* buf = (${cTypeFromName(inner)}*)malloc(sizeof(*buf) * n);`);
          out.push(`    if (!buf) ps_panic("R1998", "RUNTIME_OOM", "out of memory");`);
          out.push(`    for (size_t width = 1; width < n; width *= 2) {`);
          out.push(`      for (size_t left = 0; left < n; left += 2 * width) {`);
          out.push(`        size_t mid = left + width;`);
          out.push(`        size_t right = left + 2 * width;`);
          out.push(`        if (mid > n) mid = n;`);
          out.push(`        if (right > n) right = n;`);
          out.push(`        size_t i = left;`);
          out.push(`        size_t j = mid;`);
          out.push(`        size_t k = left;`);
          out.push(`        while (i < mid && j < right) {`);
          out.push(`          int cmp = 0;`);
          if (inner === "string") {
            out.push(`          ps_string a = ${n(recv)}.ptr[i];`);
            out.push(`          ps_string b = ${n(recv)}.ptr[j];`);
            out.push(`          size_t ml = (a.len < b.len) ? a.len : b.len;`);
            out.push(`          int r = memcmp(a.ptr, b.ptr, ml);`);
            out.push(`          cmp = (r < 0) ? -1 : (r > 0) ? 1 : (a.len < b.len) ? -1 : (a.len > b.len) ? 1 : 0;`);
          } else if (inner === "float") {
            out.push(`          double a = ${n(recv)}.ptr[i];`);
            out.push(`          double b = ${n(recv)}.ptr[j];`);
            out.push(`          if (isnan(a) && isnan(b)) cmp = 0;`);
            out.push(`          else if (isnan(a)) cmp = 1;`);
            out.push(`          else if (isnan(b)) cmp = -1;`);
            out.push(`          else cmp = (a < b) ? -1 : (a > b) ? 1 : 0;`);
          } else if (inner === "int" || inner === "byte") {
            out.push(`          cmp = (${n(recv)}.ptr[i] < ${n(recv)}.ptr[j]) ? -1 : (${n(recv)}.ptr[i] > ${n(recv)}.ptr[j]) ? 1 : 0;`);
          } else if (PROTO_MAP.has(inner)) {
            const owner = resolveCompareToOwner(PROTO_MAP, inner);
            if (owner) {
              const fn = cIdent(`${owner}.compareTo`);
              out.push(`          int64_t r = (int64_t)${fn}(${n(recv)}.ptr[i], ${n(recv)}.ptr[j]);`);
              out.push(`          cmp = (r < 0) ? -1 : (r > 0) ? 1 : 0;`);
            } else {
              out.push(`          ps_panic("R1010", "RUNTIME_TYPE_ERROR", "list element not comparable");`);
            }
          } else {
            out.push(`          ps_panic("R1010", "RUNTIME_TYPE_ERROR", "list element not comparable");`);
          }
          out.push(`          if (cmp <= 0) buf[k++] = ${n(recv)}.ptr[i++];`);
          out.push(`          else buf[k++] = ${n(recv)}.ptr[j++];`);
          out.push(`        }`);
          out.push(`        while (i < mid) buf[k++] = ${n(recv)}.ptr[i++];`);
          out.push(`        while (j < right) buf[k++] = ${n(recv)}.ptr[j++];`);
          out.push(`      }`);
          out.push(`      for (size_t i = 0; i < n; i++) ${n(recv)}.ptr[i] = buf[i];`);
          out.push(`    }`);
          out.push(`    free(buf);`);
          out.push(`  }`);
          out.push(`  ${n(i.dst)} = (int64_t)${n(recv)}.len;`);
          out.push(`}`);
        } else if (i.method === "removeLast" && rc && rc.kind === "list") {
          const recv = aliasOf(i.receiver) || i.receiver;
          out.push(`if (${n(recv)}.len == 0) ps_panic("R1006", "RUNTIME_EMPTY_POP", "pop on empty list");`);
          out.push(`${n(recv)}.len -= 1;`);
          out.push(`${n(recv)}.version += 1;`);
          if (t(i.dst) !== "void") out.push(`${n(i.dst)} = 0;`);
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
          if (m.valueType === "JSONValue") {
            i.pairs.forEach((p) => {
              const vt = t(p.value);
              if (vt === "JSONValue") {
                out.push(`ps_map_set_${m.base}(&${n(i.dst)}, ${n(p.key)}, ${n(p.value)});`);
              } else if (vt === "string") {
                out.push(`ps_map_set_${m.base}(&${n(i.dst)}, ${n(p.key)}, JSON_string(${n(p.value)}));`);
              } else if (vt === "bool") {
                out.push(`ps_map_set_${m.base}(&${n(i.dst)}, ${n(p.key)}, JSON_bool(${n(p.value)}));`);
              } else if (vt === "int") {
                out.push(`ps_map_set_${m.base}(&${n(i.dst)}, ${n(p.key)}, JSON_number((double)${n(p.value)}));`);
              } else if (vt === "float") {
                out.push(`ps_map_set_${m.base}(&${n(i.dst)}, ${n(p.key)}, JSON_number(${n(p.value)}));`);
              } else {
                const vc = parseContainer(vt);
                if (vc && vc.kind === "list") {
                  if (vc.inner === "JSONValue") {
                    out.push(`ps_map_set_${m.base}(&${n(i.dst)}, ${n(p.key)}, JSON_array(${n(p.value)}));`);
                  } else if (["bool", "int", "float", "string"].includes(vc.inner)) {
                    const tmp = `__json_list_${JSON_TMP_COUNTER++}`;
                    const idx = `__json_i_${JSON_TMP_COUNTER++}`;
                    out.push(`{ ps_list_JSONValue ${tmp};`);
                    out.push(`${tmp}.len = ${n(p.value)}.len;`);
                    out.push(`${tmp}.cap = ${n(p.value)}.len;`);
                    out.push(`${tmp}.ptr = ${n(p.value)}.len > 0 ? (ps_jsonvalue*)malloc(sizeof(*${tmp}.ptr) * ${n(p.value)}.len) : NULL;`);
                    out.push(`for (size_t ${idx} = 0; ${idx} < ${n(p.value)}.len; ${idx} += 1) {`);
                    if (vc.inner === "bool") out.push(`  ${tmp}.ptr[${idx}] = JSON_bool(${n(p.value)}.ptr[${idx}]);`);
                    else if (vc.inner === "int") out.push(`  ${tmp}.ptr[${idx}] = JSON_number((double)${n(p.value)}.ptr[${idx}]);`);
                    else if (vc.inner === "float") out.push(`  ${tmp}.ptr[${idx}] = JSON_number(${n(p.value)}.ptr[${idx}]);`);
                    else if (vc.inner === "string") out.push(`  ${tmp}.ptr[${idx}] = JSON_string(${n(p.value)}.ptr[${idx}]);`);
                    out.push("}");
                    out.push(`ps_map_set_${m.base}(&${n(i.dst)}, ${n(p.key)}, JSON_array(${tmp}));`);
                    out.push("}");
                  } else {
                    out.push('ps_panic("R1010", "RUNTIME_TYPE_ERROR", "JSON.encode unsupported list type");');
                  }
                } else {
                  out.push('ps_panic("R1010", "RUNTIME_TYPE_ERROR", "JSON.encode unsupported map value");');
                }
              }
            });
          } else {
            i.pairs.forEach((p) => out.push(`ps_map_set_${m.base}(&${n(i.dst)}, ${n(p.key)}, ${n(p.value)});`));
          }
        }
      }
      break;
    case "make_object":
      {
        const rt = t(i.dst);
        if (i.proto === "Exception" || i.proto === "RuntimeException") {
          const parent = i.proto === "RuntimeException" ? "Exception" : null;
          out.push(`${n(i.dst)} = (ps_exception*)calloc(1, sizeof(ps_exception));`);
          out.push(`${n(i.dst)}->type = "${i.proto}";`);
          out.push(`${n(i.dst)}->parent = ${parent ? `"${parent}"` : "NULL"};`);
          out.push(`${n(i.dst)}->is_runtime = ${i.proto === "RuntimeException" ? "1" : "0"};`);
        } else if (PROTO_MAP.has(rt)) {
          out.push(`${n(i.dst)} = (${rt}*)calloc(1, sizeof(${rt}));`);
          if (i.proto && isExceptionProto(PROTO_MAP, i.proto) && i.proto !== "Exception" && i.proto !== "RuntimeException") {
            const parent = protoParent(PROTO_MAP, i.proto);
            const isRt = isRuntimeExceptionProto(PROTO_MAP, i.proto);
            out.push(`${n(i.dst)}->base.type = "${i.proto}";`);
            out.push(`${n(i.dst)}->base.parent = ${parent ? `"${parent}"` : "NULL"};`);
            out.push(`${n(i.dst)}->base.is_runtime = ${isRt ? "1" : "0"};`);
          }
        } else out.push(`${n(i.dst)} = 0;`);
      }
      break;
    case "member_get":
      {
        const rt = t(i.target);
        const baseField = ["file", "line", "column", "message", "cause", "code", "category"].includes(i.name);
        if (rt === "Exception" || rt === "RuntimeException") {
          if (i.name === "code") out.push(`${n(i.dst)} = ${n(i.target)}->code;`);
          else if (i.name === "category") out.push(`${n(i.dst)} = ${n(i.target)}->category;`);
          else if (i.name === "message") out.push(`${n(i.dst)} = ${n(i.target)}->message;`);
          else if (i.name === "file") out.push(`${n(i.dst)} = ${n(i.target)}->file;`);
          else if (i.name === "line") out.push(`${n(i.dst)} = ${n(i.target)}->line;`);
          else if (i.name === "column") out.push(`${n(i.dst)} = ${n(i.target)}->column;`);
          else if (i.name === "cause") out.push(`${n(i.dst)} = ${n(i.target)}->cause;`);
          else out.push(`${n(i.dst)} = (ps_string){\"\",0};`);
        } else if (PROTO_MAP.has(rt) && isExceptionProto(PROTO_MAP, rt) && baseField) {
          out.push(`${n(i.dst)} = ${n(i.target)}->base.${i.name};`);
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
        const baseField = ["file", "line", "column", "message", "cause", "code", "category"].includes(i.name);
        if ((rt === "Exception" || rt === "RuntimeException") && baseField) {
          out.push(`${n(i.target)}->${i.name} = ${n(i.src)};`);
        } else if (PROTO_MAP.has(rt) && isExceptionProto(PROTO_MAP, rt) && baseField) {
          out.push(`${n(i.target)}->base.${i.name} = ${n(i.src)};`);
        } else if (PROTO_MAP.has(rt)) {
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
      {
        const vt = t(i.value);
        if (PROTO_MAP.has(vt) && isExceptionProto(PROTO_MAP, vt) && vt !== "Exception" && vt !== "RuntimeException") {
          out.push(`ps_raise_user_exception((ps_exception*)${n(i.value)});`);
        } else {
          out.push(`ps_raise_user_exception(${n(i.value)});`);
        }
      }
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
      if (t(i.source) === "string") out.push(`${n(i.dst)}.n = ps_utf8_glyph_len(${n(i.source)});`);
      else out.push(`${n(i.dst)}.n = ${n(i.source)}.len;`);
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
        else if (srcType === "string") out.push(`${n(i.dst)} = ps_string_index_glyph(${n(i.source)}, (int64_t)${n(i.iter)}.i);`);
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
  out.push(`${ret} ${cIdent(cFunctionName(fn))}(${params || "void"}) {`);

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
      const trailing = b.instrs
        .slice(idx + 1)
        .filter((i) => !["jump", "branch_if", "return", "rethrow", "break", "continue", "nop"].includes(i.op));
      postJumpInstrs.push(...trailing);
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
  const functions = dedupeFunctions(ir.functions || []);
  const irLocal = { ...ir, functions };
  const protoMap = buildProtoMap(irLocal);
  const inferred = inferTempTypes(irLocal, protoMap);
  const typeNames = collectTypeNames(irLocal, inferred);
  PROTO_MAP = protoMap;
  FN_NAMES = new Set(functions.map((fn) => fn.name));
  VARIADIC_PARAM_INFO = new Map();
  for (const fn of functions) {
    if (!fn || !Array.isArray(fn.params)) continue;
    const variadicIndex = fn.params.findIndex((p) => p.variadic);
    if (variadicIndex >= 0) {
      VARIADIC_PARAM_INFO.set(fn.name, { type: fn.params[variadicIndex].type.name, fixedCount: variadicIndex });
    }
  }
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
  out.push("#include <ctype.h>");
  out.push("#include <math.h>");
  out.push("#include <limits.h>");
  out.push("#include <setjmp.h>");
  out.push("#include <time.h>");
  out.push("#include <sys/stat.h>");
  out.push("#include <sys/wait.h>");
  out.push("#include <dirent.h>");
  out.push("#include <unistd.h>");
  out.push("#include <fcntl.h>");
  out.push("#include <poll.h>");
  out.push("#include <regex.h>");
  out.push("");
  out.push(...emitTypeDecls(typeNames, PROTO_MAP));
  out.push("");
  out.push(...emitRuntimeHelpers(PROTO_MAP));
  out.push("");
  out.push(...emitContainerHelpers(typeNames));
  out.push("");
  out.push(...emitFunctionPrototypes(irLocal));
  out.push("");
  for (const fn of functions) {
    out.push(...emitFunctionBody(fn, inferred.get(fn.name)));
    out.push("");
  }
  const mainFn = functions.find((fn) => fn.name === "main");
  if (mainFn && mainFn.params && mainFn.params.length > 0) {
    const argsType = mainFn.params[0].type?.name || "list<string>";
    const argsC = cTypeFromName(argsType);
    out.push("int main(void) {");
    out.push(`  ${argsC} args;`);
    out.push("  args.ptr = NULL;");
    out.push("  args.len = 0;");
    out.push("  args.cap = 0;");
    out.push("  args.version = 0;");
    if (mainFn.returnType.name === "void") {
      out.push(`  ${cIdent(cFunctionName(mainFn))}(args);`);
      out.push("  return 0;");
    } else {
      out.push(`  return ${cIdent(cFunctionName(mainFn))}(args);`);
    }
    out.push("}");
    out.push("");
  }
  return `${out.join("\n")}\n`;
}

function cFunctionName(fn) {
  if (fn.name === "main" && fn.params && fn.params.length > 0) return "ps_main";
  return fn.name;
}

function dedupeFunctions(functions) {
  const map = new Map();
  for (const fn of functions) {
    if (!fn || typeof fn.name !== "string") continue;
    map.set(fn.name, fn);
  }
  return Array.from(map.values());
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
