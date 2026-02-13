"use strict";

function parseLimit(env, def) {
  if (!env) return def;
  const v = Number.parseInt(env, 10);
  if (!Number.isFinite(v) || v <= 0) return def;
  return v;
}

function isGlyph(v) {
  return v && typeof v === "object" && v.constructor && v.constructor.name === "Glyph";
}

function glyphCode(v) {
  if (!isGlyph(v)) return null;
  return v.value;
}

function isView(v) {
  return v && v.__view === true;
}

function isObjectInstance(v) {
  return v && v.__object === true;
}

function isExceptionValue(v) {
  return v && v.__exception === true;
}

function unmapKey(k) {
  if (typeof k !== "string") return k;
  if (k.startsWith("i:")) return BigInt(k.slice(2));
  if (k.startsWith("g:")) return { __glyph_proxy: true, value: Number(k.slice(2)) };
  const idx = k.indexOf(":");
  if (idx < 0) return k;
  const type = k.slice(0, idx);
  const raw = k.slice(idx + 1);
  if (type === "string") return raw;
  if (type === "boolean") return raw === "true";
  if (type === "number") return Number(raw);
  return raw;
}

function glyphString(cp) {
  const hex = cp.toString(16).toUpperCase();
  return `U+${hex.padStart(4, "0")}`;
}

function escapeGlyph(ch) {
  switch (ch) {
    case "\"": return "\\\"";
    case "\\": return "\\\\";
    case "\n": return "\\n";
    case "\r": return "\\r";
    case "\t": return "\\t";
    case "\b": return "\\b";
    case "\f": return "\\f";
    default: {
      const cp = ch.codePointAt(0);
      if (cp < 0x20) return `\\u${cp.toString(16).toUpperCase().padStart(4, "0")}`;
      return ch;
    }
  }
}

function stringifyString(s, maxGlyphs) {
  const glyphs = Array.from(s);
  const len = glyphs.length;
  const truncated = len > maxGlyphs;
  const slice = truncated ? glyphs.slice(0, maxGlyphs) : glyphs;
  const out = slice.map(escapeGlyph).join("");
  return { len, out, truncated };
}

function inferTypeOfValue(v) {
  if (typeof v === "boolean") return "bool";
  if (typeof v === "string") return "string";
  if (typeof v === "number") return "float";
  if (typeof v === "bigint") {
    if (v >= 0n && v <= 255n) return "byte";
    return "int";
  }
  if (isGlyph(v) || (v && v.__glyph_proxy)) return "glyph";
  if (Array.isArray(v)) return "list";
  if (v instanceof Map) return "map";
  if (isView(v)) return v.readonly ? "view" : "slice";
  if (isObjectInstance(v)) return v.__proto || "object";
  return "unknown";
}

function inferListType(items) {
  if (!items || items.length === 0) return "list<unknown>";
  let kind = null;
  for (const it of items) {
    const t = inferTypeOfValue(it);
    if (!kind) kind = t;
    if (kind !== t) {
      kind = "unknown";
      break;
    }
  }
  return `list<${kind || "unknown"}>`;
}

function inferMapType(entries) {
  if (!entries || entries.length === 0) return "map<unknown,unknown>";
  let keyKind = null;
  let valKind = null;
  for (const [k, v] of entries) {
    const kt = inferTypeOfValue(k);
    const vt = inferTypeOfValue(v);
    if (!keyKind) keyKind = kt;
    if (!valKind) valKind = vt;
    if (keyKind !== kt) keyKind = "unknown";
    if (valKind !== vt) valKind = "unknown";
  }
  return `map<${keyKind || "unknown"},${valKind || "unknown"}>`;
}

function inferViewType(view) {
  if (!view) return "view<unknown>";
  const kind = view.readonly ? "view" : "slice";
  const src = view.source;
  if (Array.isArray(src)) {
    const t = inferListType(src);
    const inner = t.startsWith("list<") ? t.slice(5, -1) : "unknown";
    return `${kind}<${inner}>`;
  }
  if (typeof src === "string") return `${kind}<glyph>`;
  if (isView(src) && typeof src.__type === "string") return `${kind}<${src.__type}>`;
  return `${kind}<unknown>`;
}

function parseTypeName(typeName) {
  if (!typeName || typeof typeName !== "string") return { base: "unknown", args: [] };
  const lt = typeName.indexOf("<");
  if (lt < 0) return { base: typeName, args: [] };
  const base = typeName.slice(0, lt);
  const inner = typeName.slice(lt + 1, -1);
  const args = [];
  let depth = 0;
  let start = 0;
  for (let i = 0; i < inner.length; i += 1) {
    const ch = inner[i];
    if (ch === "<") depth += 1;
    else if (ch === ">") depth -= 1;
    else if (ch === "," && depth === 0) {
      args.push(inner.slice(start, i).trim());
      start = i + 1;
    }
  }
  args.push(inner.slice(start).trim());
  return { base, args: args.filter((a) => a.length > 0) };
}

function protoChain(protoEnv, name) {
  const out = [];
  let cur = name;
  for (let i = 0; i < 64 && cur; i += 1) {
    const p = protoEnv && protoEnv.protos ? protoEnv.protos.get(cur) : protoEnv && protoEnv.get ? protoEnv.get(cur) : null;
    if (p && p.sealed) out.push(`sealed ${cur}`);
    else out.push(cur);
    if (!p || !p.parent) break;
    cur = p.parent;
  }
  return out;
}

function dump(value, opts = {}) {
  const maxDepth = parseLimit(process.env.PS_DEBUG_MAX_DEPTH, 6);
  const maxItems = parseLimit(process.env.PS_DEBUG_MAX_ITEMS, 100);
  const maxString = parseLimit(process.env.PS_DEBUG_MAX_STRING, 200);
  const protoEnv = opts.protoEnv || null;
  const groupEnv = opts.groups || null;

  const seen = new WeakMap();
  let seenId = 0;
  const out = [];

  const write = (s) => out.push(s);
  const indent = (n) => out.push(" ".repeat(n));

  const groupEntries = () => {
    if (!groupEnv) return [];
    if (groupEnv instanceof Map) return Array.from(groupEnv.entries());
    if (groupEnv.groups instanceof Map) return Array.from(groupEnv.groups.entries());
    return [];
  };

  const isGroupType = (v) =>
    v &&
    typeof v === "object" &&
    (v.__group_desc === true || v.__group_type === true || typeof v.__group_type === "string");

  const getGroupTypeName = (v) => {
    if (!isGroupType(v)) return null;
    if (typeof v.__group_type === "string") return v.__group_type;
    if (typeof v.name === "string") return v.name;
    return null;
  };

  const glyphValue = (x) => {
    if (isGlyph(x)) return x.value;
    if (x && x.__glyph_proxy) return x.value;
    return null;
  };

  const scalarEquals = (a, b) => {
    const ga = glyphValue(a);
    const gb = glyphValue(b);
    if (ga !== null || gb !== null) return ga === gb;
    if (typeof a === "bigint" && typeof b === "bigint") return a === b;
    if (typeof a === "number" && typeof b === "number") return Object.is(a, b);
    if (typeof a === "boolean" && typeof b === "boolean") return a === b;
    if (typeof a === "string" && typeof b === "string") return a === b;
    return false;
  };

  const resolveGroupValue = (v, expectedType) => {
    if (v === null || v === undefined) return null;
    if (!["boolean", "string", "number", "bigint"].includes(typeof v) && !isGlyph(v) && !(v && v.__glyph_proxy)) return null;
    for (const [gname, g] of groupEntries()) {
      const baseType = (g && g.baseType) || "unknown";
      if (expectedType && expectedType !== "unknown" && baseType !== expectedType) continue;
      const members = (g && g.members) || [];
      for (const m of members) {
        if (m && scalarEquals(v, m.runtimeValue)) {
          return { group: g, groupName: gname, member: m };
        }
      }
    }
    return null;
  };

  const dumpGroupType = (gname, g, baseIndent) => {
    const baseType = (g && g.baseType) || "unknown";
    write(`group ${baseType} ${gname} {`);
    const members = (g && g.members) || [];
    if (members.length === 0) {
      write("}");
      return;
    }
    write("\n");
    for (const m of members) {
      indent(baseIndent + 2);
      write(`${m.name} = `);
      dumpScalar(m.runtimeValue, baseType);
      write("\n");
    }
    indent(baseIndent);
    write("}");
  };

  const isRef = (v) => {
    return Array.isArray(v) || v instanceof Map || isView(v) || isObjectInstance(v) || isExceptionValue(v);
  };

  const dumpScalar = (v, expectedType) => {
    if (typeof v === "boolean") {
      write(`bool(${v ? "true" : "false"})`);
      return;
    }
    if (typeof v === "bigint") {
      if (expectedType === "byte") write(`byte(${v.toString()})`);
      else if (expectedType === "int") write(`int(${v.toString()})`);
      else if (v >= 0n && v <= 255n) write(`byte(${v.toString()})`);
      else write(`int(${v.toString()})`);
      return;
    }
    if (typeof v === "number") {
      write(`float(${Number.isFinite(v) ? v.toString() : String(v)})`);
      return;
    }
    if (isGlyph(v) || (v && v.__glyph_proxy)) {
      const cp = isGlyph(v) ? v.value : v.value;
      write(`glyph(${glyphString(cp)})`);
      return;
    }
    if (typeof v === "string") {
      const info = stringifyString(v, maxString);
      write(`string(len=${info.len}) "${info.out}"`);
      if (info.truncated) write(" … (truncated)");
      return;
    }
    write(`unknown(${typeof v})`);
  };

  const dumpValue = (v, depth, baseIndent, expectedType) => {
    if (isGroupType(v)) {
      const gname = getGroupTypeName(v);
      const g = gname
        ? groupEnv && (groupEnv.get ? groupEnv.get(gname) : groupEnv.groups && groupEnv.groups.get ? groupEnv.groups.get(gname) : null)
        : null;
      const payload = g || v;
      if (gname && payload) dumpGroupType(gname, payload, baseIndent);
      else write("unknown(group)");
      return;
    }
    if (v === null || v === undefined) {
      write("unknown(null)");
      return;
    }
    if (isRef(v)) {
      if (seen.has(v)) {
        write(`@cycle#${seen.get(v)}`);
        return;
      }
      seenId += 1;
      seen.set(v, seenId);
    }

    if (Array.isArray(v)) {
      const typeName = v.__type || inferListType(v);
      const parsed = parseTypeName(typeName);
      const elemType = parsed.args[0] || "unknown";
      write(`${typeName}(len=${v.length}) [`);
      if (depth >= maxDepth) {
        write("\n");
        indent(baseIndent + 2);
        write("… (truncated)\n");
        indent(baseIndent);
        write("]");
        return;
      }
      write("\n");
      const shown = Math.min(v.length, maxItems);
      for (let i = 0; i < shown; i += 1) {
        indent(baseIndent + 2);
        write(`[${i}] `);
        dumpValue(v[i], depth + 1, baseIndent + 2, elemType);
        write("\n");
      }
      if (v.length > shown) {
        indent(baseIndent + 2);
        write("… (truncated)\n");
      }
      indent(baseIndent);
      write("]");
      return;
    }

    if (v instanceof Map) {
      const entries = Array.from(v.entries()).map(([k, val]) => [unmapKey(k), val]);
      const typeName = v.__type || inferMapType(entries);
      const parsed = parseTypeName(typeName);
      const keyType = parsed.args[0] || "unknown";
      const valType = parsed.args[1] || "unknown";
      write(`${typeName}(len=${entries.length}) {`);
      if (depth >= maxDepth) {
        write("\n");
        indent(baseIndent + 2);
        write("… (truncated)\n");
        indent(baseIndent);
        write("}");
        return;
      }
      write("\n");
      const shown = Math.min(entries.length, maxItems);
      for (let i = 0; i < shown; i += 1) {
        const [k, val] = entries[i];
        indent(baseIndent + 2);
        write("[");
        if (typeof k === "string") {
          const info = stringifyString(k, maxString);
          write(`"${info.out}"`);
          if (info.truncated) write(" … (truncated)");
        } else {
          dumpScalar(k, keyType);
        }
        write("] ");
        dumpValue(val, depth + 1, baseIndent + 2, valType);
        write("\n");
      }
      if (entries.length > shown) {
        indent(baseIndent + 2);
        write("… (truncated)\n");
      }
      indent(baseIndent);
      write("}");
      return;
    }

    if (isView(v)) {
      const typeName = v.__type || inferViewType(v);
      const parsed = parseTypeName(typeName);
      const elemType = parsed.args[0] || "unknown";
      write(`${typeName}(len=${v.len}) [`);
      if (depth >= maxDepth) {
        write("\n");
        indent(baseIndent + 2);
        write("… (truncated)\n");
        indent(baseIndent);
        write("]");
        return;
      }
      write("\n");
      const shown = Math.min(v.len, maxItems);
      for (let i = 0; i < shown; i += 1) {
        indent(baseIndent + 2);
        write(`[${i}] `);
        if (Array.isArray(v.source)) {
          dumpValue(v.source[v.offset + i], depth + 1, baseIndent + 2, elemType);
        } else if (typeof v.source === "string") {
          const ch = Array.from(v.source)[v.offset + i] || "";
          dumpScalar({ __glyph_proxy: true, value: ch.codePointAt(0) || 0 }, elemType);
        } else {
          write("unknown(view)");
        }
        write("\n");
      }
      if (v.len > shown) {
        indent(baseIndent + 2);
        write("… (truncated)\n");
      }
      indent(baseIndent);
      write("]");
      return;
    }

    if (isObjectInstance(v)) {
      const protoName = v.__proto || "unknown";
      write(`object<${protoName}> {`);
      if (depth >= maxDepth) {
        write("\n");
        indent(baseIndent + 2);
        write("… (truncated)\n");
        indent(baseIndent);
        write("}");
        return;
      }
      write("\n");
      indent(baseIndent + 2);
      const chain = protoChain(protoEnv, protoName);
      write(`delegation: ${chain.length ? chain.join(" -> ") : "<unknown>"}`);
      write("\n");
      indent(baseIndent + 2);
      write("fields:\n");
      const protoMap = protoEnv && protoEnv.protos ? protoEnv.protos : protoEnv;
      const getProto = (name) => protoMap && protoMap.get ? protoMap.get(name) : null;
      const typeToString = (t) => {
        if (!t) return "unknown";
        if (typeof t === "string") return t;
        if (t.kind === "PrimitiveType" || t.kind === "NamedType") return t.name;
        if (t.kind === "GenericType") {
          const args = (t.args || []).map(typeToString).join(",");
          return `${t.name}<${args}>`;
        }
        return t.name || "unknown";
      };
      let cur = getProto(protoName);
      for (let guard = 0; cur && guard < 64; guard += 1) {
        for (const f of cur.fields || []) {
          indent(baseIndent + 4);
          write(`[${cur.name}] ${f.name} : ${typeToString(f.type)} = `);
          const val = v.__fields ? v.__fields[f.name] : undefined;
          if (val === undefined) {
            write("unknown(missing)");
          } else {
            dumpValue(val, depth + 1, baseIndent + 4, typeToString(f.type));
          }
          write("\n");
        }
        if (!cur.parent) break;
        cur = getProto(cur.parent);
      }
      indent(baseIndent + 2);
      write("methods:\n");
      cur = getProto(protoName);
      const findOverride = (protoName, methodName) => {
        let p = getProto(protoName);
        if (!p) return null;
        p = p.parent ? getProto(p.parent) : null;
        while (p) {
          const methods = p.methods instanceof Map ? Array.from(p.methods.values()) : p.methods || [];
          if (methods.some((m) => m.name === methodName)) return p.name;
          p = p.parent ? getProto(p.parent) : null;
        }
        return null;
      };
      for (let guard = 0; cur && guard < 64; guard += 1) {
        const methods = cur.methods instanceof Map ? Array.from(cur.methods.values()) : cur.methods || [];
        for (const m of methods) {
          indent(baseIndent + 4);
          write(`[${cur.name}] ${m.name}(`);
          const params = m.params || [];
          for (let i = 0; i < params.length; i += 1) {
            if (i > 0) write(", ");
            const p = params[i];
            const pname = p.name || "";
            const ptype = typeToString(p.type);
            if (p.variadic) write(`...${pname}:${ptype}`);
            else write(`${pname}:${ptype}`);
          }
          const ret = typeToString(m.retType || m.returnType);
          write(`) : ${ret}`);
          const over = findOverride(cur.name, m.name);
          if (over) write(`  (overrides ${over}.${m.name})`);
          write("\n");
        }
        if (!cur.parent) break;
        cur = getProto(cur.parent);
      }
      indent(baseIndent);
      write("}");
      return;
    }

    const groupMatch = resolveGroupValue(v, expectedType);
    if (groupMatch) {
      write(`${groupMatch.groupName}.${groupMatch.member.name} = `);
      dumpScalar(groupMatch.member.runtimeValue, groupMatch.group.baseType);
      return;
    }
    dumpScalar(v, expectedType);
  };

  dumpValue(value, 0, 0, null);
  write("\n");
  process.stderr.write(out.join(""));
}

module.exports = { dump };
