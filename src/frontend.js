"use strict";

const fs = require("fs");
const path = require("path");
const { optimizeIR } = require("./optimizer");
const { createDiagnostic, formatDiagnostic, pickSuggestions } = require("./diagnostics");

const KEYWORDS = new Set([
  "prototype",
  "sealed",
  "function",
  "var",
  "const",
  "internal",
  "group",
  "int",
  "float",
  "bool",
  "byte",
  "glyph",
  "string",
  "list",
  "map",
  "slice",
  "view",
  "void",
  "if",
  "else",
  "for",
  "of",
  "in",
  "while",
  "do",
  "switch",
  "case",
  "default",
  "break",
  "continue",
  "return",
  "try",
  "catch",
  "finally",
  "throw",
  "true",
  "false",
  "self",
  "super",
  "import",
  "as",
]);

class FrontendError extends Error {
  constructor(diag) {
    super(diag.message);
    this.diag = diag;
  }
}

function diag(file, line, col, code, category, message) {
  return createDiagnostic({ file, line, column: col, code, name: category, message });
}

function formatDiag(d) {
  return formatDiagnostic(d);
}

function parseTypeString(s) {
  if (!s) return { kind: "PrimitiveType", name: "void" };
  const trim = s.trim();
  const prim = ["int", "float", "bool", "byte", "glyph", "string", "void"];
  if (prim.includes(trim)) return { kind: "PrimitiveType", name: trim };
  if (trim.startsWith("list<") || trim.startsWith("slice<") || trim.startsWith("view<")) {
    const name = trim.slice(0, trim.indexOf("<"));
    const inner = trim.slice(name.length + 1, -1);
    return { kind: "GenericType", name, args: [parseTypeString(inner)] };
  }
  if (trim.startsWith("map<")) {
    const inside = trim.slice(4, -1);
    const comma = inside.indexOf(",");
    const left = inside.slice(0, comma);
    const right = inside.slice(comma + 1);
    return { kind: "GenericType", name: "map", args: [parseTypeString(left), parseTypeString(right)] };
  }
  return { kind: "NamedType", name: trim };
}

function stripWs(s) {
  return s.replace(/\s+/g, "");
}

function splitTopLevelComma(s) {
  let depth = 0;
  for (let i = 0; i < s.length; i += 1) {
    const c = s[i];
    if (c === "<") depth += 1;
    else if (c === ">") depth -= 1;
    else if (c === "," && depth === 0) return [s.slice(0, i), s.slice(i + 1)];
  }
  return null;
}

function isValidRegistryType(s, allowVoid) {
  if (typeof s !== "string") return false;
  const t = stripWs(s);
  if (allowVoid && t === "void") return true;
  const prim = [
    "int",
    "float",
    "bool",
    "byte",
    "glyph",
    "string",
    "TextFile",
    "BinaryFile",
    "JSONValue",
    "CivilDateTime",
    "PathInfo",
    "PathEntry",
    "Dir",
    "Walker",
    "RegExp",
    "RegExpMatch",
  ];
  if (prim.includes(t)) return true;
  if (t.startsWith("list<") || t.startsWith("slice<") || t.startsWith("view<")) {
    if (!t.endsWith(">")) return false;
    const name = t.slice(0, t.indexOf("<"));
    const inner = t.slice(name.length + 1, -1);
    return inner.length > 0 && isValidRegistryType(inner, false);
  }
  if (t.startsWith("map<")) {
    if (!t.endsWith(">")) return false;
    const inside = t.slice(4, -1);
    const parts = splitTopLevelComma(inside);
    if (!parts) return false;
    return isValidRegistryType(parts[0], false) && isValidRegistryType(parts[1], false);
  }
  return false;
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
  let data = null;
  let registryPath = null;
  for (const c of candidates) {
    if (c && fs.existsSync(c)) {
      data = fs.readFileSync(c, "utf8");
      registryPath = c;
      break;
    }
  }
  if (!data) return null;
  let doc = null;
  try {
    doc = JSON.parse(data);
  } catch {
    return null;
  }
  const modules = new Map();
  const searchPaths = Array.isArray(doc.search_paths) ? doc.search_paths.filter((p) => typeof p === "string") : [];
  const list = Array.isArray(doc.modules) ? doc.modules : [];
  for (const m of list) {
    if (!m || typeof m.name !== "string") continue;
    const fns = new Map();
    const consts = new Map();
    for (const f of m.functions || []) {
      if (!f || typeof f.name !== "string") continue;
      const paramsRaw = Array.isArray(f.params) ? f.params : [];
      const validParams = paramsRaw.every((p) => isValidRegistryType(p, false));
      const validRet = isValidRegistryType(f.ret ?? "void", true);
      const params = paramsRaw.map(parseTypeString);
      const retType = parseTypeString(f.ret);
      const ast = {
        kind: "FunctionDecl",
        name: f.name,
        params: params.map((t, i) => ({ kind: "Param", type: t, name: `p${i}`, variadic: false })),
        retType,
        body: { kind: "Block", stmts: [] },
      };
      fns.set(f.name, { name: f.name, params, retType, ast, valid: validParams && validRet });
    }
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
      }
    }
    modules.set(m.name, { functions: fns, constants: consts });
  }
  return { modules, searchPaths, registryPath };
}

class Lexer {
  constructor(file, src) {
    this.file = file;
    this.src = src;
    this.i = 0;
    this.line = 1;
    this.col = 1;
    this.tokens = [];
  }

  eof() {
    return this.i >= this.src.length;
  }

  ch(offset = 0) {
    return this.src[this.i + offset] || "";
  }

  advance() {
    const c = this.ch();
    this.i += 1;
    if (c === "\n") {
      this.line += 1;
      this.col = 1;
    } else {
      this.col += 1;
    }
    return c;
  }

  add(type, value, line, col) {
    this.tokens.push({ type, value, line, col });
  }

  lex() {
    while (!this.eof()) {
      const c = this.ch();
      if (c === " " || c === "\t" || c === "\r" || c === "\n") {
        this.advance();
        continue;
      }
      if (c === "/" && this.ch(1) === "/") {
        while (!this.eof() && this.ch() !== "\n") this.advance();
        continue;
      }
      if (c === "/" && this.ch(1) === "*") {
        const sl = this.line;
        const sc = this.col;
        this.advance();
        this.advance();
        while (!this.eof()) {
          if (this.ch() === "*" && this.ch(1) === "/") {
            this.advance();
            this.advance();
            break;
          }
          this.advance();
        }
        if (this.eof() && !(this.src[this.i - 2] === "*" && this.src[this.i - 1] === "/")) {
          throw new FrontendError(
            diag(this.file, sl, sc, "E1002", "PARSE_UNCLOSED_BLOCK", "Unclosed block comment")
          );
        }
        continue;
      }

      const line = this.line;
      const col = this.col;

      if (/[A-Za-z_]/.test(c)) {
        let s = "";
        while (/[A-Za-z0-9_]/.test(this.ch())) s += this.advance();
        if (KEYWORDS.has(s)) this.add("kw", s, line, col);
        else this.add("id", s, line, col);
        continue;
      }

      if (c === "." && /[0-9]/.test(this.ch(1))) {
        let s = "";
        s += this.advance();
        while (/[0-9]/.test(this.ch())) s += this.advance();
        if (/[eE]/.test(this.ch())) {
          s += this.advance();
          if (/[+-]/.test(this.ch())) s += this.advance();
          while (/[0-9]/.test(this.ch())) s += this.advance();
        }
        this.add("num", s, line, col);
        continue;
      }

      if (/[0-9]/.test(c)) {
        let s = "";
        if (c === "0" && (this.ch(1) === "x" || this.ch(1) === "X")) {
          s += this.advance();
          s += this.advance();
          while (/[0-9A-Fa-f]/.test(this.ch())) s += this.advance();
        } else if (c === "0" && (this.ch(1) === "b" || this.ch(1) === "B")) {
          s += this.advance();
          s += this.advance();
          while (/[01]/.test(this.ch())) s += this.advance();
        } else {
          while (/[0-9]/.test(this.ch())) s += this.advance();
          if (this.ch() === ".") {
            s += this.advance();
            while (/[0-9]/.test(this.ch())) s += this.advance();
          }
          if (/[eE]/.test(this.ch())) {
            s += this.advance();
            if (/[+-]/.test(this.ch())) s += this.advance();
            while (/[0-9]/.test(this.ch())) s += this.advance();
          }
        }
        this.add("num", s, line, col);
        continue;
      }

      if (c === '"') {
        this.advance();
        let s = "";
        while (!this.eof() && this.ch() !== '"') {
          if (this.ch() === "\\") {
            const escLine = this.line;
            const escCol = this.col;
            this.advance();
            if (this.eof()) {
              throw new FrontendError(diag(this.file, escLine, escCol, "E1001", "PARSE_UNEXPECTED_TOKEN", "invalid escape sequence"));
            }
            const esc = this.ch();
            this.advance();
            switch (esc) {
              case '"':
                s += '"';
                break;
              case "\\":
                s += "\\";
                break;
              case "n":
                s += "\n";
                break;
              case "t":
                s += "\t";
                break;
              case "r":
                s += "\r";
                break;
              case "b":
                s += "\b";
                break;
              case "f":
                s += "\f";
                break;
              case "u": {
                let hex = "";
                for (let i = 0; i < 4; i += 1) {
                  if (this.eof()) {
                    throw new FrontendError(
                      diag(this.file, escLine, escCol, "E1001", "PARSE_UNEXPECTED_TOKEN", "invalid escape sequence")
                    );
                  }
                  const h = this.ch();
                  if (!/[0-9A-Fa-f]/.test(h)) {
                    throw new FrontendError(
                      diag(this.file, escLine, escCol, "E1001", "PARSE_UNEXPECTED_TOKEN", "invalid escape sequence")
                    );
                  }
                  hex += h;
                  this.advance();
                }
                const cp = parseInt(hex, 16);
                if (cp >= 0xd800 && cp <= 0xdfff) {
                  throw new FrontendError(
                    diag(this.file, escLine, escCol, "E1001", "PARSE_UNEXPECTED_TOKEN", "invalid escape sequence")
                  );
                }
                s += String.fromCodePoint(cp);
                break;
              }
              default:
                throw new FrontendError(diag(this.file, escLine, escCol, "E1001", "PARSE_UNEXPECTED_TOKEN", "invalid escape sequence"));
            }
            continue;
          }
          s += this.advance();
        }
        if (this.eof()) {
          throw new FrontendError(diag(this.file, line, col, "E1002", "PARSE_UNCLOSED_BLOCK", "Unclosed string literal"));
        }
        this.advance();
        this.add("str", s, line, col);
        continue;
      }

      const three = this.src.slice(this.i, this.i + 3);
      if (three === "...") {
        this.advance();
        this.advance();
        this.advance();
        this.add("sym", "...", line, col);
        continue;
      }

      const two = this.src.slice(this.i, this.i + 2);
      const twoSyms = new Set([
        "==",
        "!=",
        "<=",
        ">=",
        "&&",
        "||",
        "<<",
        ">>",
        "++",
        "--",
        "+=",
        "-=",
        "*=",
        "/=",
      ]);
      if (twoSyms.has(two)) {
        this.advance();
        this.advance();
        this.add("sym", two, line, col);
        continue;
      }

      const oneSyms = new Set([
        "{",
        "}",
        "(",
        ")",
        "[",
        "]",
        ";",
        ",",
        ":",
        ".",
        "?",
        "+",
        "-",
        "*",
        "/",
        "%",
        "&",
        "|",
        "^",
        "~",
        "!",
        "=",
        "<",
        ">",
      ]);
      if (oneSyms.has(c)) {
        this.advance();
        this.add("sym", c, line, col);
        continue;
      }

      throw new FrontendError(
        diag(this.file, line, col, "E1001", "PARSE_UNEXPECTED_TOKEN", `Unexpected character '${c}'`)
      );
    }

    this.add("eof", "eof", this.line, this.col);
    return this.tokens;
  }
}

class Parser {
  constructor(file, tokens) {
    this.file = file;
    this.tokens = tokens;
    this.i = 0;
  }

  t(offset = 0) {
    return this.tokens[this.i + offset] || this.tokens[this.tokens.length - 1];
  }

  at(type, value = null) {
    const tok = this.t();
    if (tok.type !== type) return false;
    return value === null ? true : tok.value === value;
  }

  eat(type, value = null) {
    if (!this.at(type, value)) {
      const tok = this.t();
      const expected = value === null ? type : value;
      throw new FrontendError(
        diag(
          this.file,
          tok.line,
          tok.col,
          "E1001",
          "PARSE_UNEXPECTED_TOKEN",
          `unexpected token '${tok.value}', expecting '${expected}'`
        )
      );
    }
    const tok = this.t();
    this.i += 1;
    return tok;
  }

  parseProgram() {
    const decls = [];
    const imports = [];
    while (!this.at("eof")) {
      if (this.at("kw", "import")) imports.push(this.parseImportDecl());
      else if (this.at("kw", "sealed") || this.at("kw", "prototype")) decls.push(this.parsePrototypeDecl());
      else if (this.looksLikeGroupDecl()) decls.push(this.parseGroupDecl());
      else if (this.at("kw", "function")) decls.push(this.parseFunctionDecl());
      else if (this.at("kw", "internal")) {
        const tok = this.eat("kw", "internal");
        if (this.at("kw", "function")) {
          const fn = this.parseFunctionDecl();
          fn.visibility = "internal";
          decls.push(fn);
          continue;
        }
        throw new FrontendError(
          diag(this.file, tok.line, tok.col, "E3200", "INVALID_VISIBILITY_LOCATION", "keyword 'internal' is only allowed in prototype members")
        );
      }
      else {
        const tok = this.t();
        throw new FrontendError(
          diag(this.file, tok.line, tok.col, "E1001", "PARSE_UNEXPECTED_TOKEN", "Top-level declaration expected")
        );
      }
    }
    return { kind: "Program", imports, decls };
  }

  parsePrototypeDecl() {
    let sealed = false;
    if (this.at("kw", "sealed")) {
      const s = this.eat("kw", "sealed");
      sealed = true;
      if (!this.at("kw", "prototype")) {
        throw new FrontendError(
          diag(this.file, s.line, s.col, "E1001", "PARSE_UNEXPECTED_TOKEN", "prototype expected after sealed")
        );
      }
    }
    const start = this.eat("kw", "prototype");
    const name = this.eat("id").value;
    let parent = null;
    if (this.at("sym", ":")) {
      this.eat("sym", ":");
      parent = this.eat("id").value;
    }
    this.eat("sym", "{");
    const fields = [];
    const methods = [];
    while (!this.at("sym", "}")) {
      let visibility = "public";
      if (this.at("kw", "internal")) {
        this.eat("kw", "internal");
        visibility = "internal";
      }
      if (
        visibility === "internal" &&
        !this.at("kw", "function") &&
        !this.at("kw", "const") &&
        !this.at("id") &&
        !(this.at("kw") && ["int", "float", "bool", "byte", "glyph", "string", "list", "map", "slice", "view"].includes(this.t().value))
      ) {
        const tok = this.t();
        throw new FrontendError(
          diag(this.file, tok.line, tok.col, "E3200", "INVALID_VISIBILITY_LOCATION", "keyword 'internal' is only allowed on prototype fields or methods")
        );
      }
      if (this.at("kw", "function")) {
        const m = this.parseFunctionDecl();
        m.visibility = visibility;
        methods.push(m);
        continue;
      }
      let isConst = false;
      if (this.at("kw", "const")) {
        this.eat("kw", "const");
        isConst = true;
      }
      const t = this.parseType();
      const n = this.eat("id");
      let init = null;
      if (this.at("sym", "=")) {
        this.eat("sym", "=");
        init = this.parseExpr();
      }
      this.eat("sym", ";");
      fields.push({ kind: "FieldDecl", type: t, name: n.value, init, isConst, visibility, line: n.line, col: n.col });
    }
    this.eat("sym", "}");
    return { kind: "PrototypeDecl", name, parent, fields, methods, sealed, moduleFile: this.file, line: start.line, col: start.col };
  }

  parseModulePath() {
    const parts = [];
    const first = this.at("id") || this.at("kw") ? this.eat(this.t().type) : null;
    if (!first) {
      const tok = this.t();
      throw new FrontendError(
        diag(this.file, tok.line, tok.col, "E1001", "PARSE_UNEXPECTED_TOKEN", `unexpected token '${tok.value}', expecting 'id'`)
      );
    }
    parts.push(first.value);
    while (this.at("sym", ".") && (this.t(1).type === "id" || this.t(1).type === "kw")) {
      this.eat("sym", ".");
      const seg = this.eat(this.t().type);
      parts.push(seg.value);
    }
    return parts;
  }

  parseImportDecl() {
    const start = this.eat("kw", "import");
    let modulePath = null;
    let pathLiteral = null;
    let isPath = false;
    if (this.at("str")) {
      pathLiteral = this.eat("str").value;
      isPath = true;
    } else {
      modulePath = this.parseModulePath();
    }
    let alias = null;
    let items = null;
    if (this.at("kw", "as")) {
      this.eat("kw", "as");
      alias = this.eat("id").value;
    } else if (this.at("sym", ".") && this.t(1).value === "{") {
      this.eat("sym", ".");
      this.eat("sym", "{");
      items = [];
      if (!this.at("sym", "}")) {
        items.push(this.parseImportItem());
        while (this.at("sym", ",")) {
          this.eat("sym", ",");
          items.push(this.parseImportItem());
        }
      }
      this.eat("sym", "}");
    }
    this.eat("sym", ";");
    return { kind: "ImportDecl", modulePath, path: pathLiteral, isPath, alias, items, line: start.line, col: start.col };
  }

  parseImportItem() {
    const name = this.eat("id");
    let alias = null;
    if (this.at("kw", "as")) {
      this.eat("kw", "as");
      alias = this.eat("id").value;
    }
    return { name: name.value, alias, line: name.line, col: name.col };
  }

  parseFunctionDecl() {
    const start = this.eat("kw", "function");
    const name = this.eat("id").value;
    this.eat("sym", "(");
    const params = [];
    if (!this.at("sym", ")")) {
      params.push(this.parseParam());
      while (this.at("sym", ",")) {
        this.eat("sym", ",");
        params.push(this.parseParam());
      }
    }
    this.eat("sym", ")");
    this.eat("sym", ":");
    const retType = this.parseType();
    const body = this.parseBlock();
    return { kind: "FunctionDecl", name, params, retType, body, line: start.line, col: start.col };
  }

  parseParam() {
    const t = this.parseType();
    const name = this.eat("id");
    let variadic = false;
    if (this.at("sym", "...")) {
      this.eat("sym", "...");
      variadic = true;
    }
    return { kind: "Param", type: t, name: name.value, variadic, line: name.line, col: name.col };
  }

  parseType() {
    const tok = this.t();
    if (tok.type !== "kw" && tok.type !== "id") {
      throw new FrontendError(
        diag(this.file, tok.line, tok.col, "E1001", "PARSE_UNEXPECTED_TOKEN", "Type expected")
      );
    }
    if (tok.type === "id") {
      this.i += 1;
      return { kind: "NamedType", name: tok.value };
    }
    const kw = tok.value;
    this.i += 1;
    if (["int", "float", "bool", "byte", "glyph", "string", "void"].includes(kw)) {
      return { kind: "PrimitiveType", name: kw };
    }
    if (["list", "slice", "view"].includes(kw)) {
      this.eat("sym", "<");
      const inner = this.parseType();
      this.eat("sym", ">");
      return { kind: "GenericType", name: kw, args: [inner] };
    }
    if (kw === "map") {
      this.eat("sym", "<");
      const k = this.parseType();
      this.eat("sym", ",");
      const v = this.parseType();
      this.eat("sym", ">");
      return { kind: "GenericType", name: "map", args: [k, v] };
    }
    throw new FrontendError(
      diag(this.file, tok.line, tok.col, "E1001", "PARSE_UNEXPECTED_TOKEN", `Unknown type '${kw}'`)
    );
  }

  parseBlock() {
    const l = this.eat("sym", "{");
    const stmts = [];
    while (!this.at("sym", "}")) {
      if (this.at("eof")) {
        throw new FrontendError(
          diag(this.file, l.line, l.col, "E1002", "PARSE_UNCLOSED_BLOCK", "Unclosed block")
        );
      }
      stmts.push(this.parseStmt());
    }
    this.eat("sym", "}");
    return { kind: "Block", stmts, line: l.line, col: l.col };
  }

  parseStmt() {
    if (this.at("sym", "{")) return this.parseBlock();
    if (this.at("kw", "var")) return this.parseVarDeclStmt();
    if (this.at("kw", "const")) return this.parseVarDeclStmt();
    if (this.looksLikeTypedVarDecl()) return this.parseVarDeclStmt();
    if (this.at("kw", "if")) return this.parseIfStmt();
    if (this.at("kw", "while")) return this.parseWhileStmt();
    if (this.at("kw", "do")) return this.parseDoWhileStmt();
    if (this.at("kw", "for")) return this.parseForStmt();
    if (this.at("kw", "switch")) return this.parseSwitchStmt();
    if (this.at("kw", "try")) return this.parseTryStmt();
    if (this.at("kw", "return")) return this.parseReturnStmt();
    if (this.at("kw", "break")) return this.parseSimpleStmt("BreakStmt", "break");
    if (this.at("kw", "continue")) return this.parseSimpleStmt("ContinueStmt", "continue");
    if (this.at("kw", "throw")) return this.parseThrowStmt();
    if (this.at("kw", "internal")) {
      const tok = this.t();
      throw new FrontendError(
        diag(this.file, tok.line, tok.col, "E3200", "INVALID_VISIBILITY_LOCATION", "keyword 'internal' is only allowed in prototype members")
      );
    }
    if (this.looksLikeAssignStmt()) return this.parseAssignStmt(true);
    return this.parseExprStmt();
  }

  parseIfStmt() {
    const t = this.eat("kw", "if");
    this.eat("sym", "(");
    const cond = this.parseExpr();
    this.eat("sym", ")");
    const thenBranch = this.parseStmt();
    let elseBranch = null;
    if (this.at("kw", "else")) {
      this.eat("kw", "else");
      if (this.at("kw", "if")) elseBranch = this.parseIfStmt();
      else elseBranch = this.parseStmt();
    }
    return { kind: "IfStmt", cond, thenBranch, elseBranch, line: t.line, col: t.col };
  }

  parseWhileStmt() {
    const t = this.eat("kw", "while");
    this.eat("sym", "(");
    const cond = this.parseExpr();
    this.eat("sym", ")");
    const body = this.parseStmt();
    return { kind: "WhileStmt", cond, body, line: t.line, col: t.col };
  }

  parseDoWhileStmt() {
    const t = this.eat("kw", "do");
    const body = this.parseStmt();
    this.eat("kw", "while");
    this.eat("sym", "(");
    const cond = this.parseExpr();
    this.eat("sym", ")");
    this.eat("sym", ";");
    return { kind: "DoWhileStmt", cond, body, line: t.line, col: t.col };
  }

  looksLikeTypedVarDecl() {
    const a = this.t();
    const b = this.t(1);
    if (!(a.type === "kw" || a.type === "id")) return false;
    if (!["int", "float", "bool", "byte", "glyph", "string", "list", "map", "slice", "view"].includes(a.value) && a.type !== "id") {
      return false;
    }
    if (a.value === "list" || a.value === "map" || a.value === "slice" || a.value === "view") return true;
    return b.type === "id";
  }

  parseVarDeclStmt() {
    const d = this.parseVarDecl();
    this.eat("sym", ";");
    return d;
  }

  parseVarDecl() {
    if (this.at("kw", "const")) {
      const t = this.eat("kw", "const");
      const declaredType = this.parseType();
      const name = this.eat("id");
      let init = null;
      if (this.at("sym", "=")) {
        this.eat("sym", "=");
        init = this.parseExpr();
      }
      return { kind: "VarDecl", declaredType, name: name.value, init, isConst: true, line: t.line, col: t.col };
    }
    if (this.at("kw", "var")) {
      const t = this.eat("kw", "var");
      const name = this.eat("id");
      this.eat("sym", "=");
      const init = this.parseExpr();
      return { kind: "VarDecl", declaredType: null, name: name.value, init, line: t.line, col: t.col };
    }
    const declaredType = this.parseType();
    const name = this.eat("id");
    let init = null;
    if (this.at("sym", "=")) {
      this.eat("sym", "=");
      init = this.parseExpr();
    }
    return { kind: "VarDecl", declaredType, name: name.value, init, line: name.line, col: name.col };
  }

  looksLikeGroupDecl() {
    const end = this.scanTypeFrom(this.i);
    if (end < 0) return false;
    const next = this.tokens[end];
    return !!next && next.type === "kw" && next.value === "group";
  }

  scanTypeFrom(start) {
    const tok = this.tokens[start];
    if (!tok || (tok.type !== "kw" && tok.type !== "id")) return -1;
    if (tok.type === "id") return start + 1;
    const kw = tok.value;
    if (["int", "float", "bool", "byte", "glyph", "string", "void"].includes(kw)) return start + 1;
    if (["list", "slice", "view"].includes(kw)) {
      let i = start + 1;
      if (!this.tokens[i] || this.tokens[i].type !== "sym" || this.tokens[i].value !== "<") return -1;
      i = this.scanTypeFrom(i + 1);
      if (i < 0) return -1;
      if (!this.tokens[i] || this.tokens[i].type !== "sym" || this.tokens[i].value !== ">") return -1;
      return i + 1;
    }
    if (kw === "map") {
      let i = start + 1;
      if (!this.tokens[i] || this.tokens[i].type !== "sym" || this.tokens[i].value !== "<") return -1;
      i = this.scanTypeFrom(i + 1);
      if (i < 0) return -1;
      if (!this.tokens[i] || this.tokens[i].type !== "sym" || this.tokens[i].value !== ",") return -1;
      i = this.scanTypeFrom(i + 1);
      if (i < 0) return -1;
      if (!this.tokens[i] || this.tokens[i].type !== "sym" || this.tokens[i].value !== ">") return -1;
      return i + 1;
    }
    return -1;
  }

  parseGroupDecl() {
    const start = this.t();
    const groupType = this.parseType();
    this.eat("kw", "group");
    const name = this.eat("id");
    this.eat("sym", "{");
    const members = [];
    if (!this.at("sym", "}")) {
      members.push(this.parseGroupMember());
      while (this.at("sym", ",")) {
        this.eat("sym", ",");
        if (this.at("sym", "}")) break;
        members.push(this.parseGroupMember());
      }
    }
    this.eat("sym", "}");
    return {
      kind: "GroupDecl",
      type: groupType,
      name: name.value,
      members,
      line: start.line,
      col: start.col,
    };
  }

  parseGroupMember() {
    if (this.at("kw", "internal")) {
      const tok = this.t();
      throw new FrontendError(
        diag(this.file, tok.line, tok.col, "E3200", "INVALID_VISIBILITY_LOCATION", "keyword 'internal' is forbidden in group declarations")
      );
    }
    const name = this.eat("id");
    this.eat("sym", "=");
    const expr = this.parseExpr();
    return { kind: "GroupMember", name: name.value, expr, line: name.line, col: name.col };
  }

  parseForStmt() {
    const t = this.eat("kw", "for");
    this.eat("sym", "(");
    let kind = "classic";
    let init = null;
    let cond = null;
    let step = null;
    let iterVar = null;
    let iterExpr = null;

    const mark = this.i;
    if ((this.at("kw", "var") || this.looksLikeTypeStart()) && this.findUntil(["of", "in"], ")")) {
      const left = this.at("kw", "var") ? this.parseVarIterDecl() : this.parseTypedIterDecl();
      if (this.at("kw", "of")) {
        this.eat("kw", "of");
        iterExpr = this.parseExpr();
        kind = "of";
        iterVar = left;
      } else if (this.at("kw", "in")) {
        this.eat("kw", "in");
        iterExpr = this.parseExpr();
        kind = "in";
        iterVar = left;
      } else {
        this.i = mark;
      }
    }
    if (kind === "classic") {
      if (!this.at("sym", ";")) {
        if (this.at("kw", "var") || this.looksLikeTypedVarDecl()) init = this.parseVarDecl();
        else if (this.looksLikeAssignStmt()) init = this.parseAssignStmt(false);
        else init = this.parseExpr();
      }
      this.eat("sym", ";");
      if (!this.at("sym", ";")) cond = this.parseExpr();
      this.eat("sym", ";");
      if (!this.at("sym", ")")) {
        if (this.looksLikeAssignStmt()) step = this.parseAssignStmt(false);
        else step = this.parseExpr();
      }
    }
    this.eat("sym", ")");
    const body = this.parseStmt();
    return { kind: "ForStmt", forKind: kind, init, cond, step, iterVar, iterExpr, body, line: t.line, col: t.col };
  }

  parseVarIterDecl() {
    this.eat("kw", "var");
    const id = this.eat("id");
    return { kind: "IterVar", declaredType: null, name: id.value, line: id.line, col: id.col };
  }

  parseTypedIterDecl() {
    const t = this.parseType();
    const id = this.eat("id");
    return { kind: "IterVar", declaredType: t, name: id.value, line: id.line, col: id.col };
  }

  parseSwitchStmt() {
    const t = this.eat("kw", "switch");
    this.eat("sym", "(");
    const expr = this.parseExpr();
    this.eat("sym", ")");
    this.eat("sym", "{");
    const cases = [];
    let defaultCase = null;
    while (!this.at("sym", "}")) {
      if (this.at("kw", "case")) {
        const c = this.eat("kw", "case");
        const value = this.parseExpr();
        this.eat("sym", ":");
        const stmts = [];
        while (!this.at("kw", "case") && !this.at("kw", "default") && !this.at("sym", "}")) {
          stmts.push(this.parseStmt());
        }
        cases.push({ kind: "CaseClause", value, stmts, line: c.line, col: c.col });
        continue;
      }
      if (this.at("kw", "default")) {
        const d = this.eat("kw", "default");
        this.eat("sym", ":");
        const stmts = [];
        while (!this.at("kw", "case") && !this.at("sym", "}")) {
          stmts.push(this.parseStmt());
        }
        defaultCase = { kind: "DefaultClause", stmts, line: d.line, col: d.col };
        continue;
      }
      const tok = this.t();
      throw new FrontendError(
        diag(this.file, tok.line, tok.col, "E1001", "PARSE_UNEXPECTED_TOKEN", "Expected 'case' or 'default'")
      );
    }
    this.eat("sym", "}");
    return { kind: "SwitchStmt", expr, cases, defaultCase, line: t.line, col: t.col };
  }

  parseReturnStmt() {
    const t = this.eat("kw", "return");
    let expr = null;
    if (!this.at("sym", ";")) expr = this.parseExpr();
    this.eat("sym", ";");
    return { kind: "ReturnStmt", expr, line: t.line, col: t.col };
  }

  parseThrowStmt() {
    const t = this.eat("kw", "throw");
    const expr = this.parseExpr();
    this.eat("sym", ";");
    return { kind: "ThrowStmt", expr, line: t.line, col: t.col };
  }

  parseTryStmt() {
    const t = this.eat("kw", "try");
    const tryBlock = this.parseBlock();
    const catches = [];
    while (this.at("kw", "catch")) {
      this.eat("kw", "catch");
      this.eat("sym", "(");
      const type = this.parseType();
      const name = this.eat("id");
      this.eat("sym", ")");
      const block = this.parseBlock();
      catches.push({ kind: "CatchClause", type, name: name.value, block, line: name.line, col: name.col });
    }
    let finallyBlock = null;
    if (this.at("kw", "finally")) {
      this.eat("kw", "finally");
      finallyBlock = this.parseBlock();
    }
    if (catches.length === 0 && !finallyBlock) {
      throw new FrontendError(diag(this.file, t.line, t.col, "E1001", "PARSE_UNEXPECTED_TOKEN", "catch or finally expected"));
    }
    return { kind: "TryStmt", tryBlock, catches, finallyBlock, line: t.line, col: t.col };
  }

  parseSimpleStmt(kind, kw) {
    const t = this.eat("kw", kw);
    this.eat("sym", ";");
    return { kind, line: t.line, col: t.col };
  }

  parseAssignStmt(withSemi) {
    const start = this.t();
    const target = this.parsePostfixExpr();
    const op = this.eat("sym").value;
    if (!["=", "+=", "-=", "*=", "/="].includes(op)) {
      const tok = this.t(-1);
      throw new FrontendError(
        diag(this.file, tok.line, tok.col, "E1001", "PARSE_UNEXPECTED_TOKEN", "Assignment operator expected")
      );
    }
    const expr = this.parseConditionalExpr();
    if (withSemi) this.eat("sym", ";");
    return { kind: "AssignStmt", target, op, expr, line: start.line, col: start.col };
  }

  parseExprStmt() {
    const expr = this.parseExpr();
    this.eat("sym", ";");
    return { kind: "ExprStmt", expr, line: expr.line, col: expr.col };
  }

  parseExpr() {
    return this.parseConditionalExpr();
  }

  parseConditionalExpr() {
    let cond = this.parseOrExpr();
    if (this.at("sym", "?")) {
      const q = this.eat("sym", "?");
      const thenExpr = this.parseConditionalExpr();
      this.eat("sym", ":");
      const elseExpr = this.parseConditionalExpr();
      return { kind: "ConditionalExpr", cond, thenExpr, elseExpr, line: q.line, col: q.col };
    }
    return cond;
  }

  parseOrExpr() {
    let expr = this.parseAndExpr();
    while (this.at("sym", "||")) {
      const op = this.eat("sym", "||");
      expr = { kind: "BinaryExpr", op: op.value, left: expr, right: this.parseAndExpr(), line: op.line, col: op.col };
    }
    return expr;
  }

  parseAndExpr() {
    let expr = this.parseEqExpr();
    while (this.at("sym", "&&")) {
      const op = this.eat("sym", "&&");
      expr = { kind: "BinaryExpr", op: op.value, left: expr, right: this.parseEqExpr(), line: op.line, col: op.col };
    }
    return expr;
  }

  parseEqExpr() {
    let expr = this.parseRelExpr();
    while (this.at("sym", "==") || this.at("sym", "!=")) {
      const op = this.eat("sym");
      expr = { kind: "BinaryExpr", op: op.value, left: expr, right: this.parseRelExpr(), line: op.line, col: op.col };
    }
    return expr;
  }

  parseRelExpr() {
    let expr = this.parseShiftExpr();
    while (["<", "<=", ">", ">="].includes(this.t().value)) {
      const op = this.eat("sym");
      expr = { kind: "BinaryExpr", op: op.value, left: expr, right: this.parseShiftExpr(), line: op.line, col: op.col };
    }
    return expr;
  }

  parseShiftExpr() {
    let expr = this.parseAddExpr();
    while (this.at("sym", "<<") || this.at("sym", ">>")) {
      const op = this.eat("sym");
      expr = { kind: "BinaryExpr", op: op.value, left: expr, right: this.parseAddExpr(), line: op.line, col: op.col };
    }
    return expr;
  }

  parseAddExpr() {
    let expr = this.parseMulExpr();
    while (["+", "-", "|", "^"].includes(this.t().value)) {
      const op = this.eat("sym");
      expr = { kind: "BinaryExpr", op: op.value, left: expr, right: this.parseMulExpr(), line: op.line, col: op.col };
    }
    return expr;
  }

  parseMulExpr() {
    let expr = this.parseUnaryExpr();
    while (["*", "/", "%", "&"].includes(this.t().value)) {
      const op = this.eat("sym");
      expr = { kind: "BinaryExpr", op: op.value, left: expr, right: this.parseUnaryExpr(), line: op.line, col: op.col };
    }
    return expr;
  }

  parseUnaryExpr() {
    if (this.at("sym", "(") && this.t(1).type === "kw" && ["int", "float", "byte"].includes(this.t(1).value)) {
      const lp = this.eat("sym", "(");
      const targetType = this.parseType();
      this.eat("sym", ")");
      const expr = this.parseUnaryExpr();
      return { kind: "CastExpr", targetType, expr, line: lp.line, col: lp.col };
    }
    if (["!", "~", "-", "++", "--"].includes(this.t().value)) {
      const op = this.eat("sym");
      return { kind: "UnaryExpr", op: op.value, expr: this.parsePostfixExpr(), line: op.line, col: op.col };
    }
    return this.parsePostfixExpr();
  }

  parsePostfixExpr() {
    let expr = this.parsePrimaryExpr();
    while (true) {
      if (this.at("sym", "(")) {
        const lp = this.eat("sym", "(");
        const args = [];
        if (!this.at("sym", ")")) {
          args.push(this.parseExpr());
          while (this.at("sym", ",")) {
            this.eat("sym", ",");
            args.push(this.parseExpr());
          }
        }
        this.eat("sym", ")");
        expr = { kind: "CallExpr", callee: expr, args, line: lp.line, col: lp.col };
      } else if (this.at("sym", "[")) {
        const lb = this.eat("sym", "[");
        const index = this.parseExpr();
        this.eat("sym", "]");
        expr = { kind: "IndexExpr", target: expr, index, line: lb.line, col: lb.col };
      } else if (this.at("sym", ".")) {
        const d = this.eat("sym", ".");
        const name = this.parseMemberName();
        expr = { kind: "MemberExpr", target: expr, name, line: d.line, col: d.col };
      } else if (this.at("sym", "++") || this.at("sym", "--")) {
        const op = this.eat("sym").value;
        expr = { kind: "PostfixExpr", op, expr, line: expr.line, col: expr.col };
      } else {
        break;
      }
    }
    return expr;
  }

  parsePrimaryExpr() {
    const tok = this.t();
    if (tok.type === "num") {
      this.i += 1;
      const isFloat = tok.value.includes(".") || /[eE]/.test(tok.value);
      return { kind: "Literal", literalType: isFloat ? "float" : "int", value: tok.value, line: tok.line, col: tok.col };
    }
    if (tok.type === "str") {
      this.i += 1;
      return { kind: "Literal", literalType: "string", value: tok.value, line: tok.line, col: tok.col };
    }
    if (tok.type === "kw" && (tok.value === "true" || tok.value === "false")) {
      this.i += 1;
      return { kind: "Literal", literalType: "bool", value: tok.value === "true", line: tok.line, col: tok.col };
    }
    if (tok.type === "kw" && tok.value === "super") {
      this.i += 1;
      return { kind: "SuperExpr", line: tok.line, col: tok.col };
    }
    if (tok.type === "id" || (tok.type === "kw" && tok.value === "self")) {
      this.i += 1;
      return { kind: "Identifier", name: tok.value, line: tok.line, col: tok.col };
    }
    if (this.at("sym", "(")) {
      this.eat("sym", "(");
      const expr = this.parseExpr();
      this.eat("sym", ")");
      return expr;
    }
    if (this.at("sym", "[")) {
      const lb = this.eat("sym", "[");
      const items = [];
      if (!this.at("sym", "]")) {
        items.push(this.parseExpr());
        while (this.at("sym", ",")) {
          this.eat("sym", ",");
          items.push(this.parseExpr());
        }
      }
      this.eat("sym", "]");
      return { kind: "ListLiteral", items, line: lb.line, col: lb.col };
    }
    if (this.at("sym", "{")) {
      const lb = this.eat("sym", "{");
      const pairs = [];
      if (!this.at("sym", "}")) {
        pairs.push(this.parseMapPair());
        while (this.at("sym", ",")) {
          this.eat("sym", ",");
          pairs.push(this.parseMapPair());
        }
      }
      this.eat("sym", "}");
      return { kind: "MapLiteral", pairs, line: lb.line, col: lb.col };
    }
    throw new FrontendError(
      diag(this.file, tok.line, tok.col, "E1001", "PARSE_UNEXPECTED_TOKEN", `unexpected token '${tok.value}', expecting 'expression'`)
    );
  }

  parseMapPair() {
    const key = this.parseExpr();
    this.eat("sym", ":");
    const value = this.parseExpr();
    return { kind: "MapPair", key, value };
  }

  parseMemberName() {
    const tok = this.t();
    if (tok.type === "id" || tok.type === "kw") {
      this.i += 1;
      return tok.value;
    }
    throw new FrontendError(
      diag(this.file, tok.line, tok.col, "E1001", "PARSE_UNEXPECTED_TOKEN", "member name expected")
    );
  }

  looksLikeTypeStart() {
    const t = this.t();
    if (t.type === "id") return true;
    return t.type === "kw" && ["int", "float", "bool", "byte", "glyph", "string", "list", "map", "slice", "view"].includes(t.value);
  }

  looksLikeAssignStmt() {
    let j = this.i;
    if (!(this.tokens[j].type === "id" || (this.tokens[j].type === "kw" && this.tokens[j].value === "self"))) return false;
    j += 1;
    while (j < this.tokens.length) {
      const tk = this.tokens[j];
      if (tk.type === "sym" && (tk.value === "." || tk.value === "[" || tk.value === "(" || tk.value === "++" || tk.value === "--")) {
        if (tk.value === "[") {
          let depth = 1;
          j += 1;
          while (j < this.tokens.length && depth > 0) {
            if (this.tokens[j].type === "sym" && this.tokens[j].value === "[") depth += 1;
            else if (this.tokens[j].type === "sym" && this.tokens[j].value === "]") depth -= 1;
            j += 1;
          }
        } else if (tk.value === "(") {
          let depth = 1;
          j += 1;
          while (j < this.tokens.length && depth > 0) {
            if (this.tokens[j].type === "sym" && this.tokens[j].value === "(") depth += 1;
            else if (this.tokens[j].type === "sym" && this.tokens[j].value === ")") depth -= 1;
            j += 1;
          }
        } else {
          j += 1;
          if (tk.value === ".") j += 1;
        }
        continue;
      }
      return tk.type === "sym" && ["=", "+=", "-=", "*=", "/="].includes(tk.value);
    }
    return false;
  }

  findUntil(symbols, stopSym) {
    for (let j = this.i; j < this.tokens.length; j += 1) {
      const tk = this.tokens[j];
      if (tk.type === "sym" && tk.value === stopSym) break;
      if (tk.type === "kw" && symbols.includes(tk.value)) return true;
    }
    return false;
  }
}

function typeToString(t) {
  if (!t) return "void";
  if (typeof t === "string") return t;
  if (t.kind === "PrimitiveType" || t.kind === "NamedType") return t.name;
  if (t.kind === "GenericType") return `${t.name}<${t.args.map(typeToString).join(",")}>`;
  return "unknown";
}

function canonicalVariadicParamType(param) {
  if (!param || !param.variadic || !param.type) return param ? param.type : null;
  if (param.type.kind === "GenericType" && param.type.name === "list" && Array.isArray(param.type.args) && param.type.args.length === 1) {
    return { kind: "GenericType", name: "view", args: [param.type.args[0]] };
  }
  return param.type;
}

const INT64_MIN = -9223372036854775808n;
const INT64_MAX = 9223372036854775807n;

function intLiteralToBigInt(expr) {
  if (!expr || expr.kind !== "Literal") return null;
  if (expr.literalType !== "int" && expr.literalType !== "number") return null;
  const raw = String(expr.value);
  if (expr.literalType === "number" && (raw.includes(".") || /[eE]/.test(raw))) return null;
  if (/^0[xX]/.test(raw)) return BigInt(raw);
  if (/^0[bB]/.test(raw)) return BigInt(raw);
  if (/^0[0-7]+$/.test(raw)) return BigInt(`0o${raw.slice(1)}`);
  return BigInt(raw);
}

function floatLiteralToNumber(expr) {
  if (!expr || expr.kind !== "Literal") return null;
  if (expr.literalType !== "float" && expr.literalType !== "number") return null;
  const raw = String(expr.value);
  if (expr.literalType === "number" && !(raw.includes(".") || /[eE]/.test(raw))) return null;
  const v = Number(raw);
  if (!Number.isFinite(v)) return null;
  return v;
}

function isNumericTypeName(name) {
  return name === "byte" || name === "int" || name === "float";
}

function isExactIntegerNumber(value) {
  return Number.isFinite(value) && Number.isInteger(value) && Number.isSafeInteger(value);
}

function isByteLiteralExpr(expr) {
  const v = intLiteralToBigInt(expr);
  return v !== null && v >= 0n && v <= 255n;
}

function isByteListLiteral(expr) {
  if (!expr || expr.kind !== "ListLiteral") return false;
  return expr.items.every((it) => isByteLiteralExpr(it));
}

function constNumericValue(expr) {
  if (!expr) return null;
  if (expr.kind === "Literal") {
    if (expr.literalType === "int" || expr.literalType === "number") {
      const v = intLiteralToBigInt(expr);
      if (v !== null) return { type: "int", value: v };
    }
    if (expr.literalType === "float" || expr.literalType === "number") {
      const v = floatLiteralToNumber(expr);
      if (v !== null) return { type: "float", value: v };
    }
  }
  if (expr.kind === "UnaryExpr" && expr.op === "-" && expr.expr) {
    const inner = constNumericValue(expr.expr);
    if (!inner) return null;
    if (inner.type === "int") return { type: "int", value: -inner.value };
    if (inner.type === "float") return { type: "float", value: -inner.value };
  }
  if (expr.kind === "CastExpr") {
    const inner = constNumericValue(expr.expr);
    if (!inner) return null;
    const target = typeToString(expr.targetType);
    if (!isNumericTypeName(target)) return null;
    if (target === "byte") {
      if (inner.type === "int") {
        if (inner.value >= 0n && inner.value <= 255n) return { type: "byte", value: inner.value };
        return null;
      }
      if (inner.type === "float") {
        if (!isExactIntegerNumber(inner.value)) return null;
        const bi = BigInt(inner.value);
        if (bi >= 0n && bi <= 255n) return { type: "byte", value: bi };
        return null;
      }
    } else if (target === "int") {
      if (inner.type === "int") {
        if (inner.value >= INT64_MIN && inner.value <= INT64_MAX) return { type: "int", value: inner.value };
        return null;
      }
      if (inner.type === "byte") {
        return { type: "int", value: inner.value };
      }
      if (inner.type === "float") {
        if (!isExactIntegerNumber(inner.value)) return null;
        const bi = BigInt(inner.value);
        if (bi >= INT64_MIN && bi <= INT64_MAX) return { type: "int", value: bi };
        return null;
      }
    } else if (target === "float") {
      if (inner.type === "int" || inner.type === "byte") {
        const num = Number(inner.value);
        if (!Number.isFinite(num)) return null;
        return { type: "float", value: num };
      }
      if (inner.type === "float") {
        if (!Number.isFinite(inner.value)) return null;
        return { type: "float", value: inner.value };
      }
    }
  }
  return null;
}

function isScalarTypeNode(t) {
  return (
    !!t &&
    t.kind === "PrimitiveType" &&
    ["bool", "byte", "glyph", "int", "float", "string"].includes(t.name)
  );
}

function evalConstExprWithOptimizer(expr, groupConsts) {
  let tempId = 0;
  const instrs = [];
  const nextTemp = () => `%c${++tempId}`;
  const emit = (i) => instrs.push(i);

  const lower = (e) => {
    if (!e) return null;
    switch (e.kind) {
      case "Literal": {
        const dst = nextTemp();
        emit({ op: "const", dst, literalType: e.literalType, value: e.value });
        return { value: dst };
      }
      case "UnaryExpr": {
        if (e.op === "++" || e.op === "--") return null;
        const inner = lower(e.expr);
        if (!inner) return null;
        const dst = nextTemp();
        emit({ op: "unary_op", dst, operator: e.op, src: inner.value });
        return { value: dst };
      }
      case "BinaryExpr": {
        const l = lower(e.left);
        const r = lower(e.right);
        if (!l || !r) return null;
        const dst = nextTemp();
        emit({ op: "bin_op", dst, operator: e.op, left: l.value, right: r.value });
        return { value: dst };
      }
      case "ConditionalExpr": {
        const c = lower(e.cond);
        const t = lower(e.thenExpr);
        const f = lower(e.elseExpr);
        if (!c || !t || !f) return null;
        const dst = nextTemp();
        emit({ op: "select", dst, cond: c.value, thenValue: t.value, elseValue: f.value });
        return { value: dst };
      }
      case "CastExpr": {
        const inner = lower(e.expr);
        if (!inner) return null;
        const dst = nextTemp();
        emit({ op: "call_method_static", dst, receiver: inner.value, method: "cast", args: [] });
        return { value: dst };
      }
      case "Identifier": {
        const dst = nextTemp();
        emit({ op: "load_var", dst, name: e.name, type: { kind: "IRType", name: "unknown" } });
        return { value: dst };
      }
      case "MemberExpr": {
        if (e.target && e.target.kind === "Identifier" && groupConsts && groupConsts.has(e.target.name)) {
          const members = groupConsts.get(e.target.name);
          if (members && members.has(e.name)) {
            const c = members.get(e.name);
            const dst = nextTemp();
            emit({ op: "const", dst, literalType: c.literalType, value: c.value });
            return { value: dst };
          }
        }
        const dst = nextTemp();
        emit({ op: "member_get", dst, target: "unknown", name: e.name });
        return { value: dst };
      }
      default: {
        const dst = nextTemp();
        emit({ op: "call_unknown", dst });
        return { value: dst };
      }
    }
  };

  const out = lower(expr);
  if (!out) return null;
  const ir = { functions: [{ blocks: [{ instrs }] }] };
  const opt = optimizeIR(ir);
  const consts = new Map();
  const ins = opt.functions[0].blocks[0].instrs || [];
  for (const i of ins) {
    if (i.op === "const") consts.set(i.dst, { literalType: i.literalType, value: i.value });
    else if (i.op === "copy") {
      if (consts.has(i.src)) consts.set(i.dst, consts.get(i.src));
      else consts.delete(i.dst);
    }
  }
  return consts.get(out.value) || null;
}

function sameType(a, b) {
  return typeToString(a) === typeToString(b);
}

class Analyzer {
  constructor(file, ast) {
    this.file = file;
    this.ast = ast;
    this.functions = new Map();
    this.prototypes = new Map();
    this.groups = new Map(); // name -> { type, members: Map(name -> const) }
    this.imported = new Map(); // local name -> { module, name, sig }
    this.namespaces = new Map(); // alias -> { kind: "native"|"proto", name }
    this.moduleConsts = new Map(); // alias -> Map(name -> { type, value })
    this.diags = [];
    this.currentModuleFile = file;
    this.currentProto = null;
  }

  withContext(moduleFile, protoName, fn) {
    const prevModule = this.currentModuleFile;
    const prevProto = this.currentProto;
    this.currentModuleFile = moduleFile || this.file;
    this.currentProto = protoName || null;
    try {
      return fn();
    } finally {
      this.currentModuleFile = prevModule;
      this.currentProto = prevProto;
    }
  }

  canAccessInternal(ownerProto) {
    if (!this.currentProto || !ownerProto) return false;
    if (this.currentProto === ownerProto) return true;
    return this.isSubtype({ kind: "NamedType", name: this.currentProto }, { kind: "NamedType", name: ownerProto });
  }

  reportVisibilityViolation(node, memberName, ownerProto) {
    const owner = ownerProto || "<unknown>";
    const caller = this.currentProto || "<global>";
    this.addDiag(
      node,
      "E3201",
      "VISIBILITY_VIOLATION",
      `member '${memberName}' is internal to prototype '${owner}' (access from prototype '${caller}')`
    );
  }

  checkMemberVisibility(node, memberInfo, memberName) {
    if (!memberInfo || memberInfo.visibility !== "internal") return true;
    if (this.canAccessInternal(memberInfo.ownerProto)) return true;
    this.reportVisibilityViolation(node, memberName, memberInfo.ownerProto);
    return false;
  }

  addDiag(node, code, category, message) {
    this.diags.push(
      createDiagnostic({
        file: this.file,
        line: node.line || 1,
        column: node.col || 1,
        code,
        name: category,
        message,
      })
    );
  }

  addDiagStructured(node, code, category, message, expectedKind, actualKind, suggestions) {
    this.diags.push(
      createDiagnostic({
        file: this.file,
        line: node.line || 1,
        column: node.col || 1,
        code,
        name: category,
        message,
        expectedKind,
        actualKind,
        suggestions,
      })
    );
  }

  collectScopeNames(scope) {
    const out = new Set();
    for (let cur = scope; cur; cur = cur.parent) {
      for (const k of cur.syms.keys()) out.add(k);
    }
    return out;
  }

  collectPrototypeMemberNames(protoName) {
    const out = new Set();
    let p = this.prototypes.get(protoName);
    while (p) {
      for (const k of p.fields.keys()) out.add(k);
      for (const k of p.methods.keys()) out.add(k);
      p = p.parent ? this.prototypes.get(p.parent) : null;
    }
    return out;
  }

  collectModuleExportNames(moduleName) {
    const out = new Set();
    const registry = loadModuleRegistry();
    const modEntry = registry ? registry.modules.get(moduleName) : null;
    if (modEntry) {
      for (const k of modEntry.functions.keys()) out.add(k);
      for (const k of modEntry.constants.keys()) out.add(k);
    }
    return out;
  }

  collectValueCandidates(scope) {
    const out = this.collectScopeNames(scope);
    for (const k of this.functions.keys()) out.add(k);
    for (const [gname, g] of this.groups.entries()) {
      out.add(gname);
      if (g && g.members) {
        for (const m of g.members.keys()) out.add(m);
      }
    }
    for (const [pname, p] of this.prototypes.entries()) {
      out.add(pname);
      for (const m of p.methods.keys()) out.add(m);
      for (const f of p.fields.keys()) out.add(f);
    }
    for (const [alias, ns] of this.namespaces.entries()) {
      out.add(alias);
      for (const ex of this.collectModuleExportNames(ns.name)) out.add(ex);
    }
    for (const k of this.imported.keys()) out.add(k);
    return [...out];
  }

  unresolvedExpected(node, name, expected, detail = null, candidates = null) {
    const suffix = `(expected ${expected})`;
    const message = detail ? `${detail} ${suffix}` : `unknown identifier '${name}' ${suffix}`;
    const suggestions = Array.isArray(candidates) ? pickSuggestions(name, candidates, 2) : [];
    this.addDiagStructured(node, "E2001", "UNRESOLVED_NAME", message, expected, null, suggestions);
  }

  unresolvedValueIdentifier(node, name, scope) {
    const candidates = this.collectValueCandidates(scope);
    if (this.groups.has(name)) {
      this.unresolvedExpected(node, name, "value", `'${name}' is a group descriptor, not a value expression`, candidates);
      return;
    }
    if (this.prototypes.has(name)) {
      this.unresolvedExpected(node, name, "value", `'${name}' is a prototype descriptor, not a value expression`, candidates);
      return;
    }
    if (this.namespaces.has(name)) {
      this.unresolvedExpected(node, name, "value", `'${name}' is a module namespace, not a value expression`, candidates);
      return;
    }
    this.unresolvedExpected(node, name, "value", null, candidates);
  }

  unresolvedMember(node, name, containerKind, containerName, expected, candidates) {
    const message = `unknown ${containerKind} '${name}' in ${containerName} (expected ${expected})`;
    const suggestions = pickSuggestions(name, candidates || [], 2);
    this.addDiagStructured(node, "E2001", "UNRESOLVED_NAME", message, expected, containerKind, suggestions);
  }

  unresolvedFunction(node, name, candidates) {
    const message = `unknown function '${name}' (expected function)`;
    const suggestions = pickSuggestions(name, candidates || [], 2);
    this.addDiagStructured(node, "E2001", "UNRESOLVED_NAME", message, "function", "identifier", suggestions);
  }

  analyze() {
    this.loadImports();
    this.collectPrototypes();
    for (const d of this.ast.decls) {
      if (d.kind === "FunctionDecl" && d.visibility === "internal") {
        this.addDiag(d, "E3200", "INVALID_VISIBILITY_LOCATION", "keyword 'internal' is only allowed in prototype members");
      }
      if (d.kind === "FunctionDecl") this.functions.set(d.name, d);
    }
    for (const [name, p] of this.prototypes.entries()) {
      for (const [mname, m] of p.methods.entries()) {
        this.functions.set(`${name}.${mname}`, m);
      }
    }
    this.collectGroups();
    this.analyzePrototypeFieldInitializers();
    for (const d of this.ast.decls) {
      if (d.kind === "FunctionDecl") this.analyzeFunction(d);
      if (d.kind === "PrototypeDecl") this.analyzePrototype(d);
    }
    return this.diags;
  }

  collectPrototypes() {
    const makeBuiltinMethod = (ownerProto, name, params, retType) => ({
      kind: "FunctionDecl",
      name,
      params: params.map((p, i) => ({ kind: "Param", name: `p${i}`, type: parseTypeString(p), variadic: false })),
      retType: parseTypeString(retType),
      body: { kind: "Block", stmts: [] },
      visibility: "public",
      _ownerProto: ownerProto,
      _moduleFile: null,
    });

    if (!this.prototypes.has("Object")) {
      const decl = { line: 1, col: 1 };
      const methods = new Map();
      methods.set("clone", makeBuiltinMethod("Object", "clone", [], "Object"));
      this.prototypes.set("Object", { decl, parent: null, fields: new Map(), methods, moduleFile: null, sealed: false });
    }

    if (!this.prototypes.has("Exception")) {
      const decl = { line: 1, col: 1 };
      const fields = new Map();
      fields.set("file", { kind: "PrimitiveType", name: "string" });
      fields.set("line", { kind: "PrimitiveType", name: "int" });
      fields.set("column", { kind: "PrimitiveType", name: "int" });
      fields.set("message", { kind: "PrimitiveType", name: "string" });
      fields.set("cause", { kind: "NamedType", name: "Exception" });
      this.prototypes.set("Exception", { decl, parent: "Object", fields, methods: new Map(), moduleFile: null });
    }
    if (!this.prototypes.has("RuntimeException")) {
      const decl = { line: 1, col: 1 };
      const fields = new Map();
      fields.set("code", { kind: "PrimitiveType", name: "string" });
      fields.set("category", { kind: "PrimitiveType", name: "string" });
      this.prototypes.set("RuntimeException", { decl, parent: "Exception", fields, methods: new Map(), moduleFile: null });
    }
    if (!this.prototypes.has("CivilDateTime")) {
      const decl = { line: 1, col: 1 };
      const methods = new Map();
      methods.set("year", makeBuiltinMethod("CivilDateTime", "year", [], "int"));
      methods.set("month", makeBuiltinMethod("CivilDateTime", "month", [], "int"));
      methods.set("day", makeBuiltinMethod("CivilDateTime", "day", [], "int"));
      methods.set("hour", makeBuiltinMethod("CivilDateTime", "hour", [], "int"));
      methods.set("minute", makeBuiltinMethod("CivilDateTime", "minute", [], "int"));
      methods.set("second", makeBuiltinMethod("CivilDateTime", "second", [], "int"));
      methods.set("millisecond", makeBuiltinMethod("CivilDateTime", "millisecond", [], "int"));
      methods.set("setYear", makeBuiltinMethod("CivilDateTime", "setYear", ["int"], "void"));
      methods.set("setMonth", makeBuiltinMethod("CivilDateTime", "setMonth", ["int"], "void"));
      methods.set("setDay", makeBuiltinMethod("CivilDateTime", "setDay", ["int"], "void"));
      methods.set("setHour", makeBuiltinMethod("CivilDateTime", "setHour", ["int"], "void"));
      methods.set("setMinute", makeBuiltinMethod("CivilDateTime", "setMinute", ["int"], "void"));
      methods.set("setSecond", makeBuiltinMethod("CivilDateTime", "setSecond", ["int"], "void"));
      methods.set("setMillisecond", makeBuiltinMethod("CivilDateTime", "setMillisecond", ["int"], "void"));
      this.prototypes.set("CivilDateTime", { decl, parent: "Object", fields: new Map(), methods, moduleFile: null });
    }
    if (!this.prototypes.has("PathInfo")) {
      const decl = { line: 1, col: 1 };
      const methods = new Map();
      methods.set("dirname", makeBuiltinMethod("PathInfo", "dirname", [], "string"));
      methods.set("basename", makeBuiltinMethod("PathInfo", "basename", [], "string"));
      methods.set("filename", makeBuiltinMethod("PathInfo", "filename", [], "string"));
      methods.set("extension", makeBuiltinMethod("PathInfo", "extension", [], "string"));
      this.prototypes.set("PathInfo", { decl, parent: "Object", fields: new Map(), methods, moduleFile: null, sealed: true });
    }
    if (!this.prototypes.has("PathEntry")) {
      const decl = { line: 1, col: 1 };
      const methods = new Map();
      methods.set("path", makeBuiltinMethod("PathEntry", "path", [], "string"));
      methods.set("name", makeBuiltinMethod("PathEntry", "name", [], "string"));
      methods.set("depth", makeBuiltinMethod("PathEntry", "depth", [], "int"));
      methods.set("isDir", makeBuiltinMethod("PathEntry", "isDir", [], "bool"));
      methods.set("isFile", makeBuiltinMethod("PathEntry", "isFile", [], "bool"));
      methods.set("isSymlink", makeBuiltinMethod("PathEntry", "isSymlink", [], "bool"));
      this.prototypes.set("PathEntry", { decl, parent: "Object", fields: new Map(), methods, moduleFile: null, sealed: true });
    }
    if (!this.prototypes.has("TextFile")) {
      const decl = { line: 1, col: 1 };
      const methods = new Map();
      methods.set("read", makeBuiltinMethod("TextFile", "read", ["int"], "string"));
      methods.set("write", makeBuiltinMethod("TextFile", "write", ["string"], "void"));
      methods.set("tell", makeBuiltinMethod("TextFile", "tell", [], "int"));
      methods.set("seek", makeBuiltinMethod("TextFile", "seek", ["int"], "void"));
      methods.set("size", makeBuiltinMethod("TextFile", "size", [], "int"));
      methods.set("name", makeBuiltinMethod("TextFile", "name", [], "string"));
      methods.set("close", makeBuiltinMethod("TextFile", "close", [], "void"));
      this.prototypes.set("TextFile", { decl, parent: "Object", fields: new Map(), methods, moduleFile: null, sealed: true });
    }
    if (!this.prototypes.has("BinaryFile")) {
      const decl = { line: 1, col: 1 };
      const methods = new Map();
      methods.set("read", makeBuiltinMethod("BinaryFile", "read", ["int"], "list<byte>"));
      methods.set("write", makeBuiltinMethod("BinaryFile", "write", ["list<byte>"], "void"));
      methods.set("tell", makeBuiltinMethod("BinaryFile", "tell", [], "int"));
      methods.set("seek", makeBuiltinMethod("BinaryFile", "seek", ["int"], "void"));
      methods.set("size", makeBuiltinMethod("BinaryFile", "size", [], "int"));
      methods.set("name", makeBuiltinMethod("BinaryFile", "name", [], "string"));
      methods.set("close", makeBuiltinMethod("BinaryFile", "close", [], "void"));
      this.prototypes.set("BinaryFile", { decl, parent: "Object", fields: new Map(), methods, moduleFile: null, sealed: true });
    }
    if (!this.prototypes.has("Dir")) {
      const decl = { line: 1, col: 1 };
      const methods = new Map();
      methods.set("hasNext", makeBuiltinMethod("Dir", "hasNext", [], "bool"));
      methods.set("next", makeBuiltinMethod("Dir", "next", [], "string"));
      methods.set("close", makeBuiltinMethod("Dir", "close", [], "void"));
      methods.set("reset", makeBuiltinMethod("Dir", "reset", [], "void"));
      this.prototypes.set("Dir", { decl, parent: "Object", fields: new Map(), methods, moduleFile: null, sealed: true });
    }
    if (!this.prototypes.has("Walker")) {
      const decl = { line: 1, col: 1 };
      const methods = new Map();
      methods.set("hasNext", makeBuiltinMethod("Walker", "hasNext", [], "bool"));
      methods.set("next", makeBuiltinMethod("Walker", "next", [], "PathEntry"));
      methods.set("close", makeBuiltinMethod("Walker", "close", [], "void"));
      this.prototypes.set("Walker", { decl, parent: "Object", fields: new Map(), methods, moduleFile: null, sealed: true });
    }
    if (!this.prototypes.has("ProcessEvent")) {
      const decl = { line: 1, col: 1 };
      const methods = new Map();
      methods.set("stream", makeBuiltinMethod("ProcessEvent", "stream", [], "int"));
      methods.set("data", makeBuiltinMethod("ProcessEvent", "data", [], "list<byte>"));
      this.prototypes.set("ProcessEvent", { decl, parent: "Object", fields: new Map(), methods, moduleFile: null, sealed: true });
    }
    if (!this.prototypes.has("ProcessResult")) {
      const decl = { line: 1, col: 1 };
      const methods = new Map();
      methods.set("exitCode", makeBuiltinMethod("ProcessResult", "exitCode", [], "int"));
      methods.set("events", makeBuiltinMethod("ProcessResult", "events", [], "list<ProcessEvent>"));
      this.prototypes.set("ProcessResult", { decl, parent: "Object", fields: new Map(), methods, moduleFile: null, sealed: true });
    }
    if (!this.prototypes.has("RegExpMatch")) {
      const decl = { line: 1, col: 1 };
      const methods = new Map();
      methods.set("ok", makeBuiltinMethod("RegExpMatch", "ok", [], "bool"));
      methods.set("start", makeBuiltinMethod("RegExpMatch", "start", [], "int"));
      methods.set("end", makeBuiltinMethod("RegExpMatch", "end", [], "int"));
      methods.set("groups", makeBuiltinMethod("RegExpMatch", "groups", [], "list<string>"));
      this.prototypes.set("RegExpMatch", { decl, parent: "Object", fields: new Map(), methods, moduleFile: null, sealed: true });
    }
    if (!this.prototypes.has("RegExp")) {
      const decl = { line: 1, col: 1 };
      const methods = new Map();
      methods.set("compile", makeBuiltinMethod("RegExp", "compile", ["string", "string"], "RegExp"));
      methods.set("test", makeBuiltinMethod("RegExp", "test", ["string", "int"], "bool"));
      methods.set("find", makeBuiltinMethod("RegExp", "find", ["string", "int"], "RegExpMatch"));
      methods.set("findAll", makeBuiltinMethod("RegExp", "findAll", ["string", "int", "int"], "list<RegExpMatch>"));
      methods.set("replaceFirst", makeBuiltinMethod("RegExp", "replaceFirst", ["string", "string", "int"], "string"));
      methods.set("replaceAll", makeBuiltinMethod("RegExp", "replaceAll", ["string", "string", "int", "int"], "string"));
      methods.set("split", makeBuiltinMethod("RegExp", "split", ["string", "int", "int"], "list<string>"));
      methods.set("pattern", makeBuiltinMethod("RegExp", "pattern", [], "string"));
      methods.set("flags", makeBuiltinMethod("RegExp", "flags", [], "string"));
      this.prototypes.set("RegExp", { decl, parent: "Object", fields: new Map(), methods, moduleFile: null, sealed: true });
    }
    const timeExceptions = [
      "DSTAmbiguousTimeException",
      "DSTNonExistentTimeException",
      "InvalidTimeZoneException",
      "InvalidDateException",
      "InvalidISOFormatException",
    ];
    const ioExceptions = [
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
    const fsExceptions = [
      "NotADirectoryException",
      "NotAFileException",
      "DirectoryNotEmptyException",
    ];
    for (const name of timeExceptions) {
      if (!this.prototypes.has(name)) {
        const decl = { line: 1, col: 1 };
        this.prototypes.set(name, { decl, parent: "Exception", fields: new Map(), methods: new Map(), moduleFile: null });
      }
    }
    for (const name of ioExceptions) {
      if (!this.prototypes.has(name)) {
        const decl = { line: 1, col: 1 };
        this.prototypes.set(name, { decl, parent: "RuntimeException", fields: new Map(), methods: new Map(), moduleFile: null });
      }
    }
    for (const name of fsExceptions) {
      if (!this.prototypes.has(name)) {
        const decl = { line: 1, col: 1 };
        this.prototypes.set(name, { decl, parent: "RuntimeException", fields: new Map(), methods: new Map(), moduleFile: null });
      }
    }
    for (const d of this.ast.decls) {
      if (d.kind !== "PrototypeDecl") continue;
      if (
        d.name === "Object" ||
        d.name === "Exception" ||
        d.name === "RuntimeException" ||
        d.name === "CivilDateTime" ||
        d.name === "PathInfo" ||
        d.name === "PathEntry" ||
        d.name === "RegExp" ||
        d.name === "RegExpMatch" ||
        d.name === "Dir" ||
        d.name === "Walker" ||
        timeExceptions.includes(d.name) ||
        ioExceptions.includes(d.name) ||
        fsExceptions.includes(d.name)
      ) {
        this.addDiag(d, "E2001", "UNRESOLVED_NAME", "reserved prototype name");
        continue;
      }
      if (this.prototypes.has(d.name)) {
        this.addDiag(d, "E2001", "UNRESOLVED_NAME", `duplicate prototype '${d.name}'`);
        continue;
      }
      const fields = new Map();
      for (const f of d.fields || []) {
        if (fields.has(f.name)) {
          this.addDiag(f, "E2001", "UNRESOLVED_NAME", `duplicate field '${f.name}' in prototype '${d.name}'`);
          continue;
        }
        fields.set(f.name, {
          type: f.type,
          isConst: !!f.isConst,
          init: f.init || null,
          visibility: f.visibility || "public",
          ownerProto: d.name,
          moduleFile: d.moduleFile || this.file,
          decl: f,
        });
      }
      const methods = new Map();
      for (const m of d.methods || []) {
        if (methods.has(m.name)) {
          this.addDiag(m, "E2001", "UNRESOLVED_NAME", `duplicate method '${m.name}' in prototype '${d.name}'`);
          continue;
        }
        m.visibility = m.visibility || "public";
        m._ownerProto = d.name;
        m._moduleFile = d.moduleFile || this.file;
        methods.set(m.name, m);
      }
      this.prototypes.set(d.name, {
        decl: d,
        parent: d.parent || "Object",
        fields,
        methods,
        sealed: !!d.sealed,
        moduleFile: d.moduleFile || this.file,
      });
    }
    for (const [name, p] of this.prototypes.entries()) {
      if (p.parent && !this.prototypes.has(p.parent)) {
        this.addDiag(
          p.decl,
          "E2001",
          "UNRESOLVED_NAME",
          `unknown parent prototype '${p.parent}' (expected prototype)`
        );
      }
      const parent = this.prototypes.get(p.parent);
      if (parent) {
        if (parent.sealed) {
          const parentName = parent.decl && parent.decl.name ? parent.decl.name : p.parent;
          this.addDiag(p.decl, "E3140", "SEALED_INHERITANCE", `cannot inherit from sealed prototype '${parentName}'`);
        }
        for (const fieldName of p.fields.keys()) {
          if (this.resolvePrototypeField(p.parent, fieldName)) {
            this.addDiag(p.decl, "E3001", "TYPE_MISMATCH_ASSIGNMENT", `field '${fieldName}' already defined in parent`);
          }
        }
        for (const [mname, m] of p.methods.entries()) {
          const pm = this.resolvePrototypeMethod(p.parent, mname);
          if (!pm) continue;
          if ((pm.visibility || "public") === "public" && (m.visibility || "public") === "internal") {
            this.addDiag(m, "E3201", "VISIBILITY_VIOLATION", `cannot override public method '${mname}' with internal visibility`);
          }
          if (!this.sameSignature(pm, m)) {
            this.addDiag(m, "E3001", "TYPE_MISMATCH_ASSIGNMENT", `override signature mismatch for '${mname}'`);
          }
        }
      }
    }
  }

  collectGroups() {
    for (const d of this.ast.decls) {
      if (d.kind !== "GroupDecl") continue;
      if (this.groups.has(d.name)) {
        this.addDiag(d, "E2001", "UNRESOLVED_NAME", `duplicate group '${d.name}'`);
        continue;
      }
      const groupType = d.type;
      if (!isScalarTypeNode(groupType)) {
        this.addDiag(d, "E3120", "GROUP_NON_SCALAR_TYPE", "group requires a scalar fundamental type");
      }
      const members = new Map();
      const groupConsts = new Map();
      groupConsts.set(d.name, members);
      for (const [gname, g] of this.groups.entries()) {
        groupConsts.set(gname, g.members);
      }
      for (const m of d.members || []) {
        if (members.has(m.name)) {
          this.addDiag(m, "E2001", "UNRESOLVED_NAME", `duplicate group member '${m.name}'`);
          continue;
        }
        const t = this.typeOfExpr(m.expr, new Scope(null));
        const targetType = groupType;
        const allowByteLiteral = targetType && typeToString(targetType) === "byte" && isByteLiteralExpr(m.expr);
        if (
          targetType &&
          t &&
          !sameType(t, targetType) &&
          !this.isSubtype(t, targetType) &&
          !allowByteLiteral
        ) {
          this.addDiag(m, "E3121", "GROUP_TYPE_MISMATCH", `group member '${m.name}' is not assignable to ${typeToString(targetType)}`);
        }
        const c = evalConstExprWithOptimizer(m.expr, groupConsts);
        if (!c) {
          this.addDiag(m, "E3121", "GROUP_TYPE_MISMATCH", "group member must be a constant expression");
        } else {
          m.constValue = c;
          members.set(m.name, c);
        }
      }
      this.groups.set(d.name, { type: groupType, members });
    }
  }

  isGroupMemberTarget(expr) {
    if (!expr || expr.kind !== "MemberExpr") return false;
    if (!expr.target || expr.target.kind !== "Identifier") return false;
    const g = this.groups.get(expr.target.name);
    return !!(g && g.members && g.members.has(expr.name));
  }

  containsSelfReference(expr) {
    if (!expr || typeof expr !== "object") return false;
    if (expr.kind === "Identifier" && expr.name === "self") return true;
    for (const k of Object.keys(expr)) {
      const v = expr[k];
      if (Array.isArray(v)) {
        for (const it of v) {
          if (it && typeof it === "object" && this.containsSelfReference(it)) return true;
        }
      } else if (v && typeof v === "object" && this.containsSelfReference(v)) {
        return true;
      }
    }
    return false;
  }

  analyzePrototypeFieldInitializers() {
    for (const d of this.ast.decls) {
      if (d.kind !== "PrototypeDecl") continue;
      for (const f of d.fields || []) {
        if (f.isConst && !isScalarTypeNode(f.type)) {
          this.addDiag(f, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "const requires a scalar fundamental type");
        }
        if (f.isConst && !f.init) {
          this.addDiag(f, "E3151", "CONST_FIELD_MISSING_INITIALIZER", "const field requires an explicit initializer");
          continue;
        }
        if (!f.init) continue;
        if (this.containsSelfReference(f.init)) {
          this.addDiag(f.init, "E3150", "INVALID_FIELD_INITIALIZER", "field initializer cannot reference 'self'");
          continue;
        }
        const initType = this.withContext(d.moduleFile || this.file, d.name, () => this.typeOfExpr(f.init, new Scope(null)));
        const targetTypeName = typeToString(f.type);
        const allowByteLiteral = targetTypeName === "byte" && isByteLiteralExpr(f.init);
        const allowByteList = targetTypeName === "list<byte>" && f.init.kind === "ListLiteral" && isByteListLiteral(f.init);
        const emptyMapInit = f.init.kind === "MapLiteral" && f.init.pairs.length === 0 && targetTypeName.startsWith("map<");
        const emptyListInit = f.init.kind === "ListLiteral" && f.init.items.length === 0 && targetTypeName.startsWith("list<");
        if (
          initType &&
          !sameType(f.type, initType) &&
          !this.isSubtype(initType, f.type) &&
          !allowByteLiteral &&
          !allowByteList &&
          !emptyMapInit &&
          !emptyListInit
        ) {
          this.addDiag(f, "E3001", "TYPE_MISMATCH_ASSIGNMENT", `cannot assign ${typeToString(initType)} to ${targetTypeName}`);
        }
      }
    }
  }

  resolvePrototypeFieldInfo(protoName, fieldName) {
    let p = this.prototypes.get(protoName);
    while (p) {
      if (p.fields.has(fieldName)) {
        const raw = p.fields.get(fieldName);
        if (raw && raw.type) return raw;
        return { type: raw, isConst: false, init: null, visibility: "public", ownerProto: protoName, moduleFile: null, decl: null };
      }
      if (!p.parent) break;
      p = this.prototypes.get(p.parent);
    }
    return null;
  }

  resolvePrototypeField(protoName, fieldName) {
    const info = this.resolvePrototypeFieldInfo(protoName, fieldName);
    return info ? info.type : null;
  }

  resolvePrototypeMethod(protoName, methodName) {
    let p = this.prototypes.get(protoName);
    while (p) {
      if (p.methods.has(methodName)) return p.methods.get(methodName);
      if (!p.parent) break;
      p = this.prototypes.get(p.parent);
    }
    return null;
  }

  sameSignature(a, b) {
    if (!a || !b) return false;
    if (a.params.length !== b.params.length) return false;
    for (let i = 0; i < a.params.length; i += 1) {
      if (!sameType(a.params[i].type, b.params[i].type)) return false;
      if (!!a.params[i].variadic !== !!b.params[i].variadic) return false;
    }
    if (a.name === "clone" && b.name === "clone") {
      return this.isSubtype(b.retType, a.retType);
    }
    return sameType(a.retType, b.retType);
  }

  isSubtype(child, parent) {
    if (!child || !parent) return false;
    const cn = typeToString(child);
    const pn = typeToString(parent);
    if (cn === pn) return true;
    let p = this.prototypes.get(cn);
    while (p) {
      if (p.parent === pn) return true;
      p = p.parent ? this.prototypes.get(p.parent) : null;
    }
    return false;
  }

  isCastRepresentable(expr, srcType, targetType) {
    const src = typeToString(srcType);
    const dst = typeToString(targetType);
    if (!isNumericTypeName(src) || !isNumericTypeName(dst)) return false;
    if (src === dst) return true;
    if (src === "byte") return dst === "int" || dst === "float" || dst === "byte";
    if (src === "int") {
      if (dst === "float") return true;
      if (dst === "byte") {
        const c = constNumericValue(expr.expr);
        if (c && (c.type === "int" || c.type === "byte")) {
          return c.value >= 0n && c.value <= 255n;
        }
        if (expr.expr && expr.expr.kind === "CastExpr" && expr.expr._castSourceType) {
          return typeToString(expr.expr._castSourceType) === "byte";
        }
        return false;
      }
    }
    if (src === "float") {
      if (dst === "float") return true;
      const c = constNumericValue(expr.expr);
      if (c && c.type === "float") {
        if (!isExactIntegerNumber(c.value)) return false;
        const bi = BigInt(c.value);
        if (dst === "int") return bi >= INT64_MIN && bi <= INT64_MAX;
        if (dst === "byte") return bi >= 0n && bi <= 255n;
      }
      if (expr.expr && expr.expr.kind === "CastExpr" && expr.expr._castSourceType) {
        const origin = typeToString(expr.expr._castSourceType);
        if (origin === "byte") return dst === "int" || dst === "byte";
      }
      return false;
    }
    return false;
  }

  loadImports() {
    const imports = this.ast.imports || [];
    if (imports.length === 0) return;
    const registry = loadModuleRegistry();
    const nativeModules = registry ? registry.modules : new Map();
    const searchPaths = registry ? registry.searchPaths : [];
    const rootDir = path.dirname(path.resolve(this.file));
    const loadedByPath = new Map();
    const loadedByName = new Map();
    const resolving = new Set();

    const resolvePathLiteral = (imp, baseFile) => {
      if (!imp.path || typeof imp.path !== "string") return null;
      if (!imp.path.endsWith(".pts")) {
        this.addDiag(imp, "E2003", "IMPORT_PATH_BAD_EXTENSION", "import path must end with .pts");
        return null;
      }
      const abs = path.isAbsolute(imp.path) ? imp.path : path.resolve(path.dirname(baseFile), imp.path);
      if (!fs.existsSync(abs)) {
        this.addDiag(imp, "E2002", "IMPORT_PATH_NOT_FOUND", "import path not found");
        return null;
      }
      return abs;
    };

    const resolveByNamePath = (modName) => {
      if (!searchPaths || searchPaths.length === 0) return null;
      const parts = modName.split(".");
      const rel = path.join(...parts) + ".pts";
      const short = parts[parts.length - 1] + ".pts";
      for (const sp of searchPaths) {
        if (typeof sp !== "string" || sp.length === 0) continue;
        const base = path.isAbsolute(sp) ? sp : path.resolve(rootDir, sp);
        const cand1 = path.join(base, rel);
        if (fs.existsSync(cand1)) return cand1;
        const cand2 = path.join(base, short);
        if (fs.existsSync(cand2)) return cand2;
      }
      return null;
    };

    const parseModuleAst = (filePath) => {
      let src = "";
      try {
        src = fs.readFileSync(filePath, "utf8");
      } catch {
        return null;
      }
      const tokens = new Lexer(filePath, src).lex();
      return new Parser(filePath, tokens).parseProgram();
    };

    const extractModulePrototype = (ast, impForDiag) => {
      let proto = null;
      let protoCount = 0;
      for (const d of ast.decls || []) {
        if (d.kind === "FunctionDecl") {
          this.addDiag(impForDiag, "E2004", "IMPORT_PATH_NO_ROOT_PROTO", "module must define exactly one root prototype");
          return null;
        }
        if (d.kind === "PrototypeDecl") {
          protoCount += 1;
          proto = d;
        }
      }
      if (protoCount !== 1 || !proto) {
        this.addDiag(impForDiag, "E2004", "IMPORT_PATH_NO_ROOT_PROTO", "module must define exactly one root prototype");
        return null;
      }
      return proto;
    };

    const loadUserModuleFromPath = (absPath, impForDiag) => {
      if (loadedByPath.has(absPath)) return loadedByPath.get(absPath);
      if (resolving.has(absPath)) {
        this.addDiag(impForDiag, "E2001", "UNRESOLVED_NAME", "cyclic module import");
        return null;
      }
      resolving.add(absPath);
      const ast = parseModuleAst(absPath);
      if (!ast) {
        this.addDiag(impForDiag, "E2002", "IMPORT_PATH_NOT_FOUND", "import path not found");
        resolving.delete(absPath);
        return null;
      }
      const protoDecl = extractModulePrototype(ast, impForDiag);
      if (!protoDecl) {
        resolving.delete(absPath);
        return null;
      }
      const methods = new Map();
      for (const m of protoDecl.methods || []) methods.set(m.name, m);
      protoDecl.moduleFile = absPath;
      const entry = { protoName: protoDecl.name, protoDecl, methods, ast, added: false };
      loadedByPath.set(absPath, entry);
      for (const subImp of ast.imports || []) resolveImport(subImp, absPath);
      if (!entry.added) {
        this.ast.decls.push(protoDecl);
        entry.added = true;
      }
      resolving.delete(absPath);
      return entry;
    };

    const addProtoImportItems = (entry, imp) => {
      for (const it of imp.items || []) {
        const local = it.alias || it.name;
        // Path modules are loaded before collectPrototypes(); resolve against the
        // loaded module entry first, then fall back to global prototype lookup.
        const m = entry.methods.get(it.name) || this.resolvePrototypeMethod(entry.protoName, it.name);
        if (!m) {
          this.unresolvedMember(it, it.name, "symbol", `module '${entry.protoName}'`, "member", this.collectPrototypeMemberNames(entry.protoName));
          continue;
        }
        if ((m.visibility || "public") === "internal") {
          this.reportVisibilityViolation(it, it.name, m._ownerProto || entry.protoName);
          continue;
        }
        const selfParam = { kind: "Param", type: { kind: "NamedType", name: entry.protoName }, name: "self", variadic: false };
        const params = [selfParam, ...m.params.map((p) => ({ ...p }))];
        const fn = {
          kind: "FunctionDecl",
          name: local,
          params,
          retType: m.retType,
          body: { kind: "Block", stmts: [] },
        };
        fn._moduleFile = this.file;
        this.imported.set(local, { module: entry.protoName, name: it.name, sig: fn, kind: "proto" });
        this.functions.set(local, fn);
      }
    };

    const resolveImport = (imp, baseFile) => {
      if (imp.isPath) {
        const abs = resolvePathLiteral(imp, baseFile);
        if (!abs) return;
        const entry = loadUserModuleFromPath(abs, imp);
        if (!entry) return;
        imp._resolved = { kind: "proto", proto: entry.protoName, path: abs };
        if (imp.items && imp.items.length > 0) {
          addProtoImportItems(entry, imp);
        } else {
          const alias = imp.alias || entry.protoName;
          this.namespaces.set(alias, { kind: "proto", name: entry.protoName });
        }
        return;
      }

      const mod = imp.modulePath.join(".");
      const modEntry = nativeModules.get(mod);
      if (modEntry) {
        imp._resolved = { kind: "native", module: mod };
        if (imp.items && imp.items.length > 0) {
          for (const it of imp.items) {
            const fn = modEntry.functions.get(it.name);
            if (!fn) {
              this.unresolvedMember(it, it.name, "symbol", `module '${mod}'`, "member", this.collectModuleExportNames(mod));
              continue;
            }
            if (!fn.valid) {
              this.addDiag(it, "E2001", "UNRESOLVED_NAME", `invalid registry signature for '${mod}.${it.name}'`);
              continue;
            }
            const local = it.alias || it.name;
            this.imported.set(local, { module: mod, name: it.name, sig: fn, kind: "native" });
            this.functions.set(local, fn.ast);
          }
        } else {
          const alias = imp.alias || imp.modulePath[imp.modulePath.length - 1];
          this.namespaces.set(alias, { kind: "native", name: mod });
          if (modEntry.constants && modEntry.constants.size > 0) {
            this.moduleConsts.set(alias, modEntry.constants);
          }
        }
        return;
      }

      const existing = loadedByName.get(mod);
      const modPath = existing ? existing.path : resolveByNamePath(mod);
      if (!modPath) {
        const moduleCandidates = registry ? [...registry.modules.keys()].sort() : [];
        this.addDiagStructured(
          imp,
          "E2001",
          "UNRESOLVED_NAME",
          `unknown module '${mod}' (expected module)`,
          "module",
          "identifier",
          pickSuggestions(mod, moduleCandidates, 2)
        );
        return;
      }
      const entry = existing ? existing.entry : loadUserModuleFromPath(modPath, imp);
      if (!entry) return;
      loadedByName.set(mod, { path: modPath, entry });
      imp._resolved = { kind: "proto", proto: entry.protoName, path: modPath };
      if (imp.items && imp.items.length > 0) {
        addProtoImportItems(entry, imp);
      } else {
        const alias = imp.alias || imp.modulePath[imp.modulePath.length - 1];
        this.namespaces.set(alias, { kind: "proto", name: entry.protoName });
      }
    };

    for (const imp of imports) resolveImport(imp, this.file);
  }

  analyzeFunction(fn) {
    const moduleFile = fn._moduleFile || this.file;
    const ownerProto = fn._ownerProto || null;
    this.withContext(moduleFile, ownerProto, () => {
      const scope = new Scope(null);
      for (const p of fn.params) {
        scope.define(p.name, canonicalVariadicParamType(p), true);
      }
      this.analyzeBlock(fn.body, scope, fn);
    });
  }

  analyzePrototype(protoDecl) {
    const protoName = protoDecl.name;
    const entry = this.prototypes.get(protoName);
    if (!entry) return;
    for (const m of entry.methods.values()) {
      const scope = new Scope(null);
      scope.define("self", { kind: "NamedType", name: protoName }, true, null, true);
      for (const p of m.params) scope.define(p.name, canonicalVariadicParamType(p), true);
      this.withContext(entry.moduleFile || this.file, protoName, () => this.analyzeBlock(m.body, scope, m));
    }
  }

  analyzeBlock(block, scope, fn) {
    const local = new Scope(scope);
    for (const s of block.stmts) this.analyzeStmt(s, local, fn);
  }

  analyzeStmt(stmt, scope, fn) {
    switch (stmt.kind) {
      case "VarDecl": {
        const isConst = !!stmt.isConst;
        let t = stmt.declaredType;
        let knownListLen = null;
        if (isConst) {
          if (!stmt.init) {
            this.addDiag(stmt, "E3130", "CONST_REASSIGNMENT", "const requires an initializer");
          }
          if (t && !isScalarTypeNode(t)) {
            this.addDiag(stmt, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "const requires a scalar fundamental type");
          }
        }
        if (stmt.init) {
          const initType = this.typeOfExpr(stmt.init, scope);
          if (
            !stmt.declaredType &&
            initType &&
            ((initType.kind === "GenericType" && initType.name === "list" && typeToString(initType) === "list<void>") ||
              (initType.kind === "GenericType" && initType.name === "map" && typeToString(initType) === "map<void,void>"))
          ) {
            this.addDiag(stmt.init, "E3006", "MISSING_TYPE_CONTEXT", "empty literal requires explicit type context");
          }
          const emptyMapInit =
            !!t &&
            stmt.init.kind === "MapLiteral" &&
            stmt.init.pairs.length === 0 &&
            typeToString(t).startsWith("map<");
          const emptyListInit =
            !!t &&
            stmt.init.kind === "ListLiteral" &&
            stmt.init.items.length === 0 &&
            typeToString(t).startsWith("list<");
          const allowByteLiteral =
            t && typeToString(t) === "byte" && stmt.init && isByteLiteralExpr(stmt.init);
          const allowByteList =
            t &&
            typeToString(t) === "list<byte>" &&
            stmt.init &&
            stmt.init.kind === "ListLiteral" &&
            isByteListLiteral(stmt.init);
          if (
            t &&
            initType &&
            !sameType(t, initType) &&
            !this.isSubtype(initType, t) &&
            !emptyMapInit &&
            !emptyListInit &&
            !allowByteLiteral &&
            !allowByteList
          ) {
            this.addDiag(stmt, "E3001", "TYPE_MISMATCH_ASSIGNMENT", `cannot assign ${typeToString(initType)} to ${typeToString(t)}`);
          }
          if (!t) t = initType;
          if (stmt.init.kind === "ListLiteral") knownListLen = stmt.init.items.length;
        }
        if (stmt.init && !t && (stmt.init.kind === "ListLiteral" || stmt.init.kind === "MapLiteral")) {
          this.addDiag(stmt, "E3006", "MISSING_TYPE_CONTEXT", "empty literal requires explicit type context");
        }
        // Variables are implicitly initialized; no uninitialized state is observable.
        const aliasSelf = stmt.init ? this.isSelfAliasExpr(stmt.init, scope) : false;
        scope.define(stmt.name, t || { kind: "PrimitiveType", name: "void" }, true, knownListLen, aliasSelf, isConst);
        break;
      }
      case "AssignStmt": {
        if (stmt.target && this.isGroupMemberTarget(stmt.target)) {
          this.addDiag(stmt, "E3122", "GROUP_MUTATION", "group members are not assignable");
          break;
        }
        if (stmt.target && stmt.target.kind === "Identifier") {
          const s = scope.lookup(stmt.target.name);
          if (s && s.isConst) {
            this.addDiag(stmt, "E3130", "CONST_REASSIGNMENT", "cannot assign to const");
            break;
          }
        }
        if (stmt.target && stmt.target.kind === "MemberExpr") {
          const targetType = this.typeOfExpr(stmt.target.target, scope);
          if (targetType && targetType.kind === "NamedType" && this.prototypes.has(targetType.name)) {
            const fi = this.resolvePrototypeFieldInfo(targetType.name, stmt.target.name);
            if (fi && fi.isConst) {
              this.addDiag(stmt, "E3130", "CONST_REASSIGNMENT", "cannot assign to const");
              break;
            }
          }
        }
        if (stmt.target && stmt.target.kind === "IndexExpr") {
          const targetType = this.typeOfExpr(stmt.target.target, scope);
          const ts = typeToString(targetType);
          if (targetType && (ts === "string" || ts.startsWith("view<"))) {
            this.addDiag(stmt, "E3004", "IMMUTABLE_INDEX_WRITE", "cannot assign through immutable index access");
            break;
          }
        }
        const lhsType = this.typeOfAssignable(stmt.target, scope);
        const rhsType = this.typeOfExpr(stmt.expr, scope);
        const emptyMapAssign =
          !!lhsType &&
          stmt.expr.kind === "MapLiteral" &&
          stmt.expr.pairs.length === 0 &&
          typeToString(lhsType).startsWith("map<");
        const emptyListAssign =
          !!lhsType &&
          stmt.expr.kind === "ListLiteral" &&
          stmt.expr.items.length === 0 &&
          typeToString(lhsType).startsWith("list<");
        if (!rhsType && stmt.expr && (stmt.expr.kind === "ListLiteral" || stmt.expr.kind === "MapLiteral") && !lhsType) {
          this.addDiag(stmt, "E3006", "MISSING_TYPE_CONTEXT", "empty literal requires explicit type context");
        } else if (
          lhsType &&
          rhsType &&
          !sameType(lhsType, rhsType) &&
          !this.isSubtype(rhsType, lhsType) &&
          !emptyMapAssign &&
          !emptyListAssign &&
          !(typeToString(lhsType) === "byte" && isByteLiteralExpr(stmt.expr)) &&
          !(typeToString(lhsType) === "list<byte>" && stmt.expr && stmt.expr.kind === "ListLiteral" && isByteListLiteral(stmt.expr))
        ) {
          this.addDiag(stmt, "E3001", "TYPE_MISMATCH_ASSIGNMENT", `cannot assign ${typeToString(rhsType)} to ${typeToString(lhsType)}`);
        }
        if (stmt.target && stmt.target.kind === "Identifier") {
          const s = scope.lookup(stmt.target.name);
          if (s) {
            if (stmt.expr && stmt.expr.kind === "ListLiteral") s.knownListLen = stmt.expr.items.length;
            else s.knownListLen = null;
            s.initialized = true;
            if (!stmt.op || stmt.op === "=") {
              s.aliasSelf = this.isSelfAliasExpr(stmt.expr, scope);
            } else {
              s.aliasSelf = false;
            }
          }
        }
        break;
      }
      case "ExprStmt":
        this.typeOfExpr(stmt.expr, scope);
        this.checkListMethodEffects(stmt.expr, scope);
        break;
      case "IfStmt": {
        const ct = this.typeOfExpr(stmt.cond, scope);
        if (ct && typeToString(ct) !== "bool") {
          this.addDiag(stmt.cond, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "condition must be bool");
        }
        this.analyzeStmt(stmt.thenBranch, new Scope(scope), fn);
        if (stmt.elseBranch) this.analyzeStmt(stmt.elseBranch, new Scope(scope), fn);
        break;
      }
      case "WhileStmt": {
        const ct = this.typeOfExpr(stmt.cond, scope);
        if (ct && typeToString(ct) !== "bool") {
          this.addDiag(stmt.cond, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "condition must be bool");
        }
        this.analyzeStmt(stmt.body, new Scope(scope), fn);
        break;
      }
      case "DoWhileStmt": {
        const ct = this.typeOfExpr(stmt.cond, scope);
        if (ct && typeToString(ct) !== "bool") {
          this.addDiag(stmt.cond, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "condition must be bool");
        }
        this.analyzeStmt(stmt.body, new Scope(scope), fn);
        break;
      }
      case "ForStmt":
        if (stmt.forKind === "classic") {
          const s2 = new Scope(scope);
          if (stmt.init) this.analyzeForPart(stmt.init, s2, fn);
          if (stmt.cond) this.typeOfExpr(stmt.cond, s2);
          if (stmt.step) this.analyzeForPart(stmt.step, s2, fn);
          this.analyzeStmt(stmt.body, s2, fn);
        } else {
          const s2 = new Scope(scope);
          const iterType = this.typeOfExpr(stmt.iterExpr, s2);
          let elementType = { kind: "PrimitiveType", name: "void" };
          const it = typeToString(iterType);
          if (it.startsWith("list<")) elementType = iterType.args[0];
          if (it.startsWith("view<")) elementType = iterType.args[0];
          if (it.startsWith("slice<")) elementType = iterType.args[0];
          if (it.startsWith("map<")) elementType = stmt.forKind === "of" ? iterType.args[1] : iterType.args[0];
          s2.define(stmt.iterVar.name, stmt.iterVar.declaredType || elementType, true);
          this.analyzeStmt(stmt.body, s2, fn);
        }
        break;
    case "SwitchStmt":
      this.typeOfExpr(stmt.expr, scope);
      for (const c of stmt.cases) {
        this.typeOfExpr(c.value, scope);
        for (const st of c.stmts) this.analyzeStmt(st, new Scope(scope), fn);
        if (!this.isTerminated(c.stmts)) {
          this.addDiag(c, "E3003", "SWITCH_CASE_NO_TERMINATION", "case without explicit termination");
        }
      }
      if (stmt.defaultCase) {
        for (const st of stmt.defaultCase.stmts) this.analyzeStmt(st, new Scope(scope), fn);
        if (!this.isTerminated(stmt.defaultCase.stmts)) {
          this.addDiag(stmt.defaultCase, "E3003", "SWITCH_CASE_NO_TERMINATION", "default without explicit termination");
        }
      }
      break;
    case "TryStmt": {
      this.analyzeBlock(stmt.tryBlock, scope, fn);
      for (const c of stmt.catches || []) {
        const cs = new Scope(scope);
        if (!this.isSubtype(c.type, { kind: "NamedType", name: "Exception" })) {
          this.addDiag(c, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "catch type must derive from Exception");
        }
        cs.define(c.name, c.type, true);
        this.analyzeBlock(c.block, cs, fn);
      }
      if (stmt.finallyBlock) this.analyzeBlock(stmt.finallyBlock, scope, fn);
      break;
    }
      case "ReturnStmt":
        if (stmt.expr && this.isSelfAliasExpr(stmt.expr, scope)) {
          this.addDiag(stmt.expr, "E3007", "INVALID_RETURN", "cannot return self");
          break;
        }
        if (stmt.expr) this.typeOfExpr(stmt.expr, scope);
        break;
      case "ThrowStmt":
        const tt = this.typeOfExpr(stmt.expr, scope);
        if (tt && !this.isSubtype(tt, { kind: "NamedType", name: "Exception" })) {
          this.addDiag(stmt, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "throw expects Exception");
        }
        break;
      case "BreakStmt":
      case "ContinueStmt":
        break;
      case "Block":
        this.analyzeBlock(stmt, scope, fn);
        break;
      default:
        break;
    }
  }

  analyzeForPart(part, scope, fn) {
    if (!part) return;
    if (part.kind === "VarDecl") this.analyzeStmt(part, scope, fn);
    else if (part.kind === "AssignStmt") this.analyzeStmt(part, scope, fn);
    else this.typeOfExpr(part, scope);
  }

  isTerminated(stmts) {
    if (!stmts || stmts.length === 0) return false;
    const last = stmts[stmts.length - 1];
    return ["BreakStmt", "ReturnStmt", "ThrowStmt"].includes(last.kind);
  }

  typeOfAssignable(expr, scope) {
    if (expr.kind === "Identifier") {
      const s = scope.lookup(expr.name);
      if (!s) {
        this.unresolvedExpected(expr, expr.name, "assignable value", null, this.collectValueCandidates(scope));
        return null;
      }
      return s.type;
    }
    if (expr.kind === "MemberExpr") {
      const targetType = this.typeOfExpr(expr.target, scope);
      if (targetType && targetType.kind === "NamedType" && this.prototypes.has(targetType.name)) {
        const fi = this.resolvePrototypeFieldInfo(targetType.name, expr.name);
        if (!fi) {
          this.unresolvedMember(expr, expr.name, "field", `prototype '${targetType.name}'`, "member", this.collectPrototypeMemberNames(targetType.name));
          return null;
        }
        if (!this.checkMemberVisibility(expr, fi, expr.name)) return null;
        return fi.type;
      }
    }
    if (expr.kind === "IndexExpr") {
      const t = this.typeOfExpr(expr.target, scope);
      const ts = typeToString(t);
      if (ts.startsWith("list<") || ts.startsWith("view<") || ts.startsWith("slice<")) return t.args[0];
      if (ts.startsWith("map<")) return t.args[1];
    }
    return this.typeOfExpr(expr, scope);
  }

  typeOfExpr(expr, scope) {
    if (!expr) return null;
    switch (expr.kind) {
      case "Literal":
        return { kind: "PrimitiveType", name: expr.literalType };
      case "SuperExpr":
        if (!this.currentProto) {
          this.addDiag(expr, "E3210", "INVALID_SUPER_USAGE", "super is only valid inside prototype methods");
          return { kind: "PrimitiveType", name: "unknown" };
        }
        return { kind: "NamedType", name: this.currentProto };
      case "CastExpr": {
        const targetType = expr.targetType;
        const targetName = typeToString(targetType);
        const srcType = this.typeOfExpr(expr.expr, scope);
        expr._castSourceType = srcType;
        expr._castTargetType = targetType;
        if (!isNumericTypeName(targetName)) {
          this.addDiag(expr, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "cast target must be numeric type");
          return targetType;
        }
        if (!srcType || !isNumericTypeName(typeToString(srcType))) {
          this.addDiag(expr, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "numeric cast requires numeric source");
          return targetType;
        }
        if (!this.isCastRepresentable(expr, srcType, targetType)) {
          this.addDiag(expr, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "numeric cast not representable");
        }
        return targetType;
      }
      case "Identifier": {
        const s = scope.lookup(expr.name);
        if (!s) {
          if (expr.name === "Sys") return { kind: "BuiltinType", name: "Sys" };
          this.unresolvedValueIdentifier(expr, expr.name, scope);
          return null;
        }
        return s.type;
      }
      case "UnaryExpr": {
        const t = this.typeOfExpr(expr.expr, scope);
        if (!t) return null;
        if (expr.op === "++" || expr.op === "--") {
          if (expr.expr && expr.expr.kind === "Identifier") {
            const s = scope.lookup(expr.expr.name);
            if (s && s.isConst) {
              this.addDiag(expr, "E3130", "CONST_REASSIGNMENT", "cannot modify const");
            }
          }
          if (expr.expr && expr.expr.kind === "MemberExpr") {
            const targetType = this.typeOfExpr(expr.expr.target, scope);
            if (targetType && targetType.kind === "NamedType" && this.prototypes.has(targetType.name)) {
              const fi = this.resolvePrototypeFieldInfo(targetType.name, expr.expr.name);
              if (fi && !this.checkMemberVisibility(expr, fi, expr.expr.name)) return t;
              if (fi && fi.isConst) this.addDiag(expr, "E3130", "CONST_REASSIGNMENT", "cannot modify const");
            }
          }
          if (this.isGroupMemberTarget(expr.expr)) {
            this.addDiag(expr, "E3122", "GROUP_MUTATION", "group members are not assignable");
          }
        }
        if (expr.op === "-" || expr.op === "++" || expr.op === "--") return t;
        if (expr.op === "!") return { kind: "PrimitiveType", name: "bool" };
        return t;
      }
      case "BinaryExpr": {
        const lt = this.typeOfExpr(expr.left, scope);
        const rt = this.typeOfExpr(expr.right, scope);
        if (!lt || !rt) return null;
        const ls = typeToString(lt);
        const rs = typeToString(rt);
        if (!sameType(lt, rt)) {
          this.addDiag(expr, "E3001", "TYPE_MISMATCH_ASSIGNMENT", `incompatible operands ${typeToString(lt)} and ${typeToString(rt)}`);
          return lt;
        }
        if (["&&", "||"].includes(expr.op) && ls !== "bool") {
          this.addDiag(expr, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "logical operators require bool operands");
        }
        if (["+", "-", "*", "/", "%"].includes(expr.op) && !["int", "float", "byte", "glyph"].includes(ls)) {
          this.addDiag(expr, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "arithmetic operators require numeric operands");
        }
        if (["&", "|", "^", "<<", ">>"].includes(expr.op) && !["int", "byte"].includes(ls)) {
          this.addDiag(expr, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "bitwise operators require int or byte operands");
        }
        if (["==", "!=", "<", "<=", ">", ">=", "&&", "||"].includes(expr.op)) {
          return { kind: "PrimitiveType", name: "bool" };
        }
        return lt;
      }
      case "ConditionalExpr": {
        const ct = this.typeOfExpr(expr.cond, scope);
        const tt = this.typeOfExpr(expr.thenExpr, scope);
        const et = this.typeOfExpr(expr.elseExpr, scope);
        if (ct && typeToString(ct) !== "bool") {
          this.addDiag(expr.cond, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "condition must be bool");
        }
        if (tt && et && !sameType(tt, et)) {
          this.addDiag(expr, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "ternary branches must have same type");
        }
        return tt || et;
      }
      case "CallExpr":
        return this.typeOfCall(expr, scope);
      case "IndexExpr": {
        const t = this.typeOfExpr(expr.target, scope);
        this.typeOfExpr(expr.index, scope);
        if (!t) return null;
        const ts = typeToString(t);
        if (ts.startsWith("list<") || ts.startsWith("view<") || ts.startsWith("slice<")) return t.args[0];
        if (ts.startsWith("map<")) return t.args[1];
        if (ts === "string") return { kind: "PrimitiveType", name: "glyph" };
        return null;
      }
      case "MemberExpr":
        if (expr.target.kind === "SuperExpr") {
          this.addDiag(expr, "E3210", "INVALID_SUPER_USAGE", "super is only valid in calls like super.method(...)");
          return null;
        }
        if (expr.target.kind === "Identifier") {
          const g = this.groups.get(expr.target.name);
          if (g && g.members) {
            if (g.members.has(expr.name)) return g.type;
            this.unresolvedMember(expr, expr.name, "group member", `group '${expr.target.name}'`, "member", [...g.members.keys()]);
            return null;
          }
        }
        if (expr.target.kind === "Identifier" && this.moduleConsts.has(expr.target.name)) {
          const consts = this.moduleConsts.get(expr.target.name);
          if (consts && consts.has(expr.name)) {
            const c = consts.get(expr.name);
            if (c.type === "float") return { kind: "PrimitiveType", name: "float" };
            if (c.type === "int") return { kind: "PrimitiveType", name: "int" };
            if (c.type === "string") return { kind: "PrimitiveType", name: "string" };
            if (c.type === "TextFile" || c.type === "BinaryFile") return { kind: "NamedType", name: c.type };
          }
        }
        {
          const targetType = this.typeOfExpr(expr.target, scope);
          if (targetType && targetType.kind === "NamedType" && this.prototypes.has(targetType.name)) {
            const fi = this.resolvePrototypeFieldInfo(targetType.name, expr.name);
            if (!fi) {
              this.unresolvedMember(expr, expr.name, "field", `prototype '${targetType.name}'`, "member", this.collectPrototypeMemberNames(targetType.name));
              return null;
            }
            if (!this.checkMemberVisibility(expr, fi, expr.name)) return null;
            return fi.type;
          }
        }
        return null;
      case "PostfixExpr":
        if (expr.op === "++" || expr.op === "--") {
          if (expr.expr && expr.expr.kind === "Identifier") {
            const s = scope.lookup(expr.expr.name);
            if (s && s.isConst) {
              this.addDiag(expr, "E3130", "CONST_REASSIGNMENT", "cannot modify const");
            }
          }
          if (expr.expr && expr.expr.kind === "MemberExpr") {
            const targetType = this.typeOfExpr(expr.expr.target, scope);
            if (targetType && targetType.kind === "NamedType" && this.prototypes.has(targetType.name)) {
              const fi = this.resolvePrototypeFieldInfo(targetType.name, expr.expr.name);
              if (fi && !this.checkMemberVisibility(expr, fi, expr.expr.name)) return this.typeOfExpr(expr.expr, scope);
              if (fi && fi.isConst) this.addDiag(expr, "E3130", "CONST_REASSIGNMENT", "cannot modify const");
            }
          }
          if (this.isGroupMemberTarget(expr.expr)) {
            this.addDiag(expr, "E3122", "GROUP_MUTATION", "group members are not assignable");
          }
        }
        return this.typeOfExpr(expr.expr, scope);
      case "ListLiteral":
        if (expr.items.length === 0) {
          return { kind: "GenericType", name: "list", args: [{ kind: "PrimitiveType", name: "void" }] };
        }
        return { kind: "GenericType", name: "list", args: [this.typeOfExpr(expr.items[0], scope)] };
      case "MapLiteral":
        if (expr.pairs.length === 0) {
          return { kind: "GenericType", name: "map", args: [{ kind: "PrimitiveType", name: "void" }, { kind: "PrimitiveType", name: "void" }] };
        }
        return {
          kind: "GenericType",
          name: "map",
          args: [this.typeOfExpr(expr.pairs[0].key, scope), this.typeOfExpr(expr.pairs[0].value, scope)],
        };
      default:
        return null;
    }
  }


  checkMethodArity(expr, t, name) {
    if (!t || !name || !expr || !Array.isArray(expr.args)) return;
    const argc = expr.args.length;
    const fail = (min, max) => {
      if (argc < min || argc > max) {
        this.addDiag(expr, "E1003", "ARITY_MISMATCH", `arity mismatch for '${name}'`);
      }
    };
    if (t === "string") {
      if (["length", "isEmpty", "toString", "toInt", "toFloat", "toUpper", "toLower", "toUtf8Bytes", "trim", "trimStart", "trimEnd"].includes(name)) {
        fail(0, 0);
        return;
      }
      if (["concat", "indexOf", "contains", "lastIndexOf", "startsWith", "endsWith", "split", "glyphAt", "repeat"].includes(name)) {
        fail(1, 1);
        return;
      }
      if (["subString", "replace", "replaceAll", "padStart", "padEnd"].includes(name)) {
        fail(2, 2);
        return;
      }
    }
    if (t === "int") {
      if (["toByte", "toFloat", "toString", "toBytes", "abs", "sign"].includes(name)) {
        fail(0, 0);
        return;
      }
    }
    if (t === "byte") {
      if (["toInt", "toFloat", "toString"].includes(name)) {
        fail(0, 0);
        return;
      }
    }
    if (t === "float") {
      if (["toInt", "toString", "toBytes", "abs", "isNaN", "isInfinite", "isFinite"].includes(name)) {
        fail(0, 0);
        return;
      }
    }
    if (t === "glyph") {
      if (["toString", "toInt", "toUtf8Bytes", "isLetter", "isDigit", "isWhitespace", "isUpper", "isLower", "toUpper", "toLower"].includes(name)) {
        fail(0, 0);
        return;
      }
    }
    if (t === "TextFile" || t === "BinaryFile") {
      if (["close", "tell", "size", "name"].includes(name)) {
        fail(0, 0);
        return;
      }
      if (name === "read" || name === "write" || name === "seek") {
        fail(1, 1);
        return;
      }
    }
    if (t.startsWith("list<")) {
      if (["length", "isEmpty", "pop", "sort", "reverse", "concat", "toUtf8String"].includes(name)) {
        fail(0, 0);
        return;
      }
      if (["push", "contains", "join"].includes(name)) {
        fail(1, 1);
        return;
      }
    }
    if (t.startsWith("map<")) {
      if (["length", "isEmpty", "keys", "values"].includes(name)) {
        fail(0, 0);
        return;
      }
      if (name === "containsKey" || name === "remove") {
        fail(1, 1);
      }
    }
  }

  checkPrototypeMethodArity(expr, methodName, params, includeSelf) {
    if (!expr || !Array.isArray(expr.args) || !Array.isArray(params)) return;
    const argc = expr.args.length;
    const fixed = params.filter((p) => !p.variadic).length;
    const hasVariadic = params.some((p) => p.variadic);
    const min = fixed + (includeSelf ? 1 : 0);
    const max = hasVariadic ? Number.POSITIVE_INFINITY : params.length + (includeSelf ? 1 : 0);
    if (argc < min || argc > max) {
      this.addDiag(expr, "E1003", "ARITY_MISMATCH", `arity mismatch for '${methodName}'`);
    }
  }

  typeOfCall(expr, scope) {
    if (expr.args) {
      for (const arg of expr.args) this.typeOfExpr(arg, scope);
    }
    if (expr.callee.kind === "Identifier") {
      const name = expr.callee.name;
      if (name === "Exception" || name === "RuntimeException") {
        this.addDiag(
          expr,
          "E2001",
          "UNRESOLVED_NAME",
          `'${name}' is a prototype descriptor, not a callable value (expected function); use ${name}.clone()`
        );
        return null;
      }
      const fn = this.functions.get(name);
      if (!fn) {
        this.unresolvedFunction(expr.callee, name, [...this.functions.keys()]);
        return null;
      }
      const variadic = fn.params.find((p) => p.variadic);
      if (!variadic) {
        if (expr.args.length !== fn.params.length) {
          this.addDiag(expr, "E1003", "ARITY_MISMATCH", `arity mismatch for '${name}'`);
        }
      } else {
        const fixed = fn.params.filter((p) => !p.variadic).length;
        if (expr.args.length < fixed) {
          this.addDiag(expr, "E1003", "ARITY_MISMATCH", `arity mismatch for '${name}'`);
        }
      }
      return fn.retType;
    }

    if (expr.callee.kind === "MemberExpr") {
      const member = expr.callee;
      if (member.target.kind === "SuperExpr") {
        if (!this.currentProto) {
          this.addDiag(member, "E3210", "INVALID_SUPER_USAGE", "super is only valid inside prototype methods");
          return null;
        }
        const owner = this.prototypes.get(this.currentProto);
        if (!owner || !owner.parent) {
          this.addDiag(member, "E3211", "SUPER_NO_PARENT", "super call requires a parent prototype");
          return null;
        }
        const pm = this.resolvePrototypeMethod(owner.parent, member.name);
        if (!pm) {
          this.addDiag(member, "E3212", "SUPER_METHOD_NOT_FOUND", `super.${member.name} not found`);
          return null;
        }
        this.checkPrototypeMethodArity(expr, member.name, pm.params, false);
        expr._superCall = { fromProto: this.currentProto };
        if (member.name === "clone") return { kind: "NamedType", name: this.currentProto };
        return pm.retType;
      }
      if (member.target.kind === "Identifier" && this.prototypes.has(member.target.name)) {
        const protoName = member.target.name;
        const pm = this.resolvePrototypeMethod(protoName, member.name);
        if (!pm) {
          this.unresolvedMember(member, member.name, "method", `prototype '${protoName}'`, "member", this.collectPrototypeMemberNames(protoName));
          return null;
        }
        if (!this.checkMemberVisibility(member, {
          visibility: pm.visibility || "public",
          ownerProto: pm._ownerProto || protoName,
          moduleFile: pm._moduleFile || (this.prototypes.get(protoName)?.moduleFile || null),
        }, member.name)) {
          return null;
        }
        const includeSelf = !((protoName === "RegExp" && member.name === "compile") || member.name === "clone");
        this.checkPrototypeMethodArity(expr, member.name, pm.params, includeSelf);
        expr._protoStatic = protoName;
        if (member.name === "clone") return { kind: "NamedType", name: protoName };
        return pm.retType;
      }
      if (member.target && member.target.kind === "Identifier") {
        const ns = this.namespaces.get(member.target.name);
        if (ns) {
          if (ns.kind === "proto") {
            const protoName = ns.name;
            const pm = this.resolvePrototypeMethod(protoName, member.name);
            if (!pm) {
              this.unresolvedMember(member, member.name, "method", `prototype '${protoName}'`, "member", this.collectPrototypeMemberNames(protoName));
              return null;
            }
            if (!this.checkMemberVisibility(member, {
              visibility: pm.visibility || "public",
              ownerProto: pm._ownerProto || protoName,
              moduleFile: pm._moduleFile || (this.prototypes.get(protoName)?.moduleFile || null),
            }, member.name)) {
              return null;
            }
            this.checkPrototypeMethodArity(expr, member.name, pm.params, member.name === "clone" ? false : true);
            expr._protoStatic = protoName;
            if (member.name === "clone") return { kind: "NamedType", name: protoName };
            return pm.retType;
          }
          const registry = loadModuleRegistry();
          const modEntry = registry ? registry.modules.get(ns.name) : null;
          const fn = modEntry ? modEntry.functions.get(member.name) : null;
          if (!fn) {
            this.unresolvedMember(member, member.name, "symbol", `module '${ns.name}'`, "member", this.collectModuleExportNames(ns.name));
            return null;
          }
          if (expr.args.length !== fn.params.length) {
            this.addDiag(expr, "E1003", "ARITY_MISMATCH", `arity mismatch for '${member.name}'`);
          }
          return fn.retType;
        }
      }
      const targetType = this.typeOfExpr(member.target, scope);
      if (!targetType) return null;
      const name = member.name;
      const prim = (n) => ({ kind: "PrimitiveType", name: n });
      if (targetType.kind === "NamedType") {
        if (targetType.name === "Dir") {
          if (name === "hasNext") return prim("bool");
          if (name === "next") return prim("string");
          if (name === "close" || name === "reset") return prim("void");
        }
        if (targetType.name === "Walker") {
          if (name === "hasNext") return prim("bool");
          if (name === "next") return { kind: "NamedType", name: "PathEntry" };
          if (name === "close") return prim("void");
        }
      }
      if (
        targetType.kind === "NamedType" &&
        this.prototypes.has(targetType.name) &&
        targetType.name !== "TextFile" &&
        targetType.name !== "BinaryFile" &&
        targetType.name !== "JSONValue"
      ) {
        const pm = this.resolvePrototypeMethod(targetType.name, member.name);
        if (!pm) {
          this.unresolvedMember(member, member.name, "method", `prototype '${targetType.name}'`, "member", this.collectPrototypeMemberNames(targetType.name));
          return null;
        }
        if (!this.checkMemberVisibility(member, {
          visibility: pm.visibility || "public",
          ownerProto: pm._ownerProto || targetType.name,
          moduleFile: pm._moduleFile || (this.prototypes.get(targetType.name)?.moduleFile || null),
        }, member.name)) {
          return null;
        }
        this.checkPrototypeMethodArity(expr, member.name, pm.params, false);
        expr._protoInstance = targetType.name;
        if (member.name === "clone") return targetType;
        return pm.retType;
      }
      const t = typeToString(targetType);
      this.checkMethodArity(expr, t, name);

      if (t === "int") {
        if (name === "toByte") return prim("byte");
        if (name === "toFloat") return prim("float");
        if (name === "toString") return prim("string");
        if (name === "toBytes") return { kind: "GenericType", name: "list", args: [prim("byte")] };
        if (name === "abs" || name === "sign") return prim("int");
      }
      if (t === "byte") {
        if (name === "toInt") return prim("int");
        if (name === "toFloat") return prim("float");
        if (name === "toString") return prim("string");
      }
      if (t === "float") {
        if (name === "toInt") return prim("int");
        if (name === "toString") return prim("string");
        if (name === "toBytes") return { kind: "GenericType", name: "list", args: [prim("byte")] };
        if (name === "abs") return prim("float");
        if (name === "isNaN" || name === "isInfinite" || name === "isFinite") return prim("bool");
      }
      if (t === "glyph") {
        if (name === "isLetter" || name === "isDigit" || name === "isWhitespace") return prim("bool");
        if (name === "isUpper" || name === "isLower") return prim("bool");
        if (name === "toUpper" || name === "toLower") return prim("glyph");
        if (name === "toString") return prim("string");
        if (name === "toInt") return prim("int");
        if (name === "toUtf8Bytes") return { kind: "GenericType", name: "list", args: [prim("byte")] };
      }
      if (t === "string") {
        if (name === "length") return prim("int");
        if (name === "isEmpty") return prim("bool");
        if (name === "toString") return prim("string");
        if (name === "toInt") return prim("int");
        if (name === "toFloat") return prim("float");
        if (name === "subString") return prim("string");
        if (name === "indexOf") return prim("int");
        if (name === "contains") return prim("bool");
        if (name === "lastIndexOf") return prim("int");
        if (name === "startsWith" || name === "endsWith") return prim("bool");
        if (name === "split") return { kind: "GenericType", name: "list", args: [prim("string")] };
        if (name === "trim" || name === "trimStart" || name === "trimEnd") return prim("string");
        if (name === "replace") return prim("string");
        if (name === "replaceAll") return prim("string");
        if (name === "glyphAt") return prim("glyph");
        if (name === "repeat") return prim("string");
        if (name === "padStart" || name === "padEnd") return prim("string");
        if (name === "toUpper" || name === "toLower") return prim("string");
        if (name === "toUtf8Bytes") return { kind: "GenericType", name: "list", args: [prim("byte")] };
        if (name === "view") {
          if (!(expr.args.length === 0 || expr.args.length === 2)) {
            this.addDiag(expr, "E1003", "ARITY_MISMATCH", "arity mismatch for 'view'");
          }
          return { kind: "GenericType", name: "view", args: [prim("glyph")] };
        }
      }
      if (t.startsWith("list<")) {
        if (name === "view" || name === "slice") {
          if (expr.args.length !== 2) {
            this.addDiag(expr, "E1003", "ARITY_MISMATCH", `arity mismatch for '${name}'`);
          }
          const elem = targetType.args[0];
          return { kind: "GenericType", name, args: [elem] };
        }
      }
      if (t.startsWith("slice<")) {
        if (name === "slice") {
          if (expr.args.length !== 2) {
            this.addDiag(expr, "E1003", "ARITY_MISMATCH", "arity mismatch for 'slice'");
          }
          const elem = targetType.args[0];
          return { kind: "GenericType", name: "slice", args: [elem] };
        }
      }
      if (t.startsWith("view<")) {
        if (name === "length") return prim("int");
        if (name === "isEmpty") return prim("bool");
        if (name === "view") {
          if (expr.args.length !== 2) {
            this.addDiag(expr, "E1003", "ARITY_MISMATCH", "arity mismatch for 'view'");
          }
          const elem = targetType.args[0];
          return { kind: "GenericType", name: "view", args: [elem] };
        }
        this.unresolvedMember(expr, name, "method", `type '${t}'`, "member", ["length", "isEmpty", "view"]);
        return null;
      }
      if (t === "TextFile") {
        if (name === "read") return prim("string");
        if (name === "write") return prim("void");
        if (name === "tell" || name === "size") return prim("int");
        if (name === "seek") return prim("void");
        if (name === "name") return prim("string");
        if (name === "close") return prim("void");
      }
      if (t === "BinaryFile") {
        if (name === "read") return { kind: "GenericType", name: "list", args: [prim("byte")] };
        if (name === "write") return prim("void");
        if (name === "tell" || name === "size") return prim("int");
        if (name === "seek") return prim("void");
        if (name === "name") return prim("string");
        if (name === "close") return prim("void");
      }
      if (t.startsWith("list<") && t === "list<string>") {
        if (name === "join") return prim("string");
        if (name === "concat") return prim("string");
      }
      if (t.startsWith("list<")) {
        if (name === "length") return prim("int");
        if (name === "isEmpty") return prim("bool");
        if (name === "push") return prim("int");
        if (name === "contains") return prim("bool");
        if (name === "reverse") return prim("int");
        if (name === "sort") {
          const elemType = targetType && targetType.kind === "GenericType" ? targetType.args[0] : null;
          const elemName = elemType ? typeToString(elemType) : "unknown";
          expr._listSortElemType = elemName;
          const okPrimitive = ["int", "float", "byte", "string"].includes(elemName);
          if (okPrimitive) return prim("int");
          if (elemType && elemType.kind === "NamedType" && this.prototypes.has(elemType.name)) {
            const pm = this.resolvePrototypeMethod(elemType.name, "compareTo");
            const paramOk =
              pm &&
              pm.params.length === 1 &&
              !pm.params[0].variadic &&
              this.isSubtype(elemType, pm.params[0].type);
            const retOk = pm && sameType(pm.retType, { kind: "PrimitiveType", name: "int" });
            if (!pm || !paramOk || !retOk) {
              this.addDiag(expr, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "list.sort requires compareTo(T other) : int");
            }
            return prim("int");
          }
          this.addDiag(expr, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "list.sort requires comparable element type");
          return prim("int");
        }
      }
      if (targetType.kind === "GenericType" && targetType.name === "map" && targetType.args.length === 2) {
        const kt = targetType.args[0];
        const vt = targetType.args[1];
        if (name === "length") return prim("int");
        if (name === "isEmpty") return prim("bool");
        if (name === "containsKey") return prim("bool");
        if (name === "remove") return prim("bool");
        if (name === "keys") return { kind: "GenericType", name: "list", args: [kt] };
        if (name === "values") return { kind: "GenericType", name: "list", args: [vt] };
      }
      if (t === "JSONValue") {
        if (name === "isNull" || name === "isBool" || name === "isNumber" || name === "isString" || name === "isArray" || name === "isObject") {
          return prim("bool");
        }
        if (name === "asBool") return prim("bool");
        if (name === "asNumber") return prim("float");
        if (name === "asString") return prim("string");
        if (name === "asArray") return { kind: "GenericType", name: "list", args: [{ kind: "NamedType", name: "JSONValue" }] };
        if (name === "asObject") {
          return {
            kind: "GenericType",
            name: "map",
            args: [{ kind: "PrimitiveType", name: "string" }, { kind: "NamedType", name: "JSONValue" }],
          };
        }
      }
      if (t.startsWith("list<") && name === "toUtf8String") return prim("string");
      return null;
    }

    return null;
  }

  checkListMethodEffects(expr, scope) {
    if (!expr || expr.kind !== "CallExpr" || !expr.callee || expr.callee.kind !== "MemberExpr") return;
    const member = expr.callee;
    if (!member.target || member.target.kind !== "Identifier") return;
    const s = scope.lookup(member.target.name);
    if (!s || !s.type) return;
    const ts = typeToString(s.type);
    if (!ts.startsWith("list<")) return;

    if (member.name === "pop") {
      if (s.knownListLen === 0) {
        this.addDiag(member.target, "E3005", "STATIC_EMPTY_POP", "pop on statically empty list");
        return;
      }
      if (typeof s.knownListLen === "number" && s.knownListLen > 0) s.knownListLen -= 1;
      else s.knownListLen = null;
      return;
    }

    if (member.name === "push") {
      if (typeof s.knownListLen === "number") s.knownListLen += 1;
      else s.knownListLen = null;
    }
  }

  isSelfAliasExpr(expr, scope) {
    if (!expr || expr.kind !== "Identifier") return false;
    if (expr.name === "self") return true;
    const s = scope.lookup(expr.name);
    return !!(s && s.aliasSelf);
  }
}

class Scope {
  constructor(parent) {
    this.parent = parent;
    this.syms = new Map();
  }
  define(name, type, initialized, knownListLen = null, aliasSelf = false, isConst = false) {
    this.syms.set(name, { type, initialized, knownListLen, aliasSelf, isConst });
  }
  lookup(name) {
    if (this.syms.has(name)) return this.syms.get(name);
    return this.parent ? this.parent.lookup(name) : null;
  }
}

function check(file, src) {
  const tokens = new Lexer(file, src).lex();
  const ast = new Parser(file, tokens).parseProgram();
  const diags = new Analyzer(file, ast).analyze();
  return diags;
}

function parseAndAnalyze(file, src) {
  const tokens = new Lexer(file, src).lex();
  const ast = new Parser(file, tokens).parseProgram();
  const diags = new Analyzer(file, ast).analyze();
  return { tokens, ast, diags };
}

function parseOnly(file, src) {
  const tokens = new Lexer(file, src).lex();
  const ast = new Parser(file, tokens).parseProgram();
  return { tokens, ast };
}

function psTypeName(typeNode) {
  if (!typeNode) return null;
  try {
    const s = typeToString(typeNode);
    return typeof s === "string" ? s : null;
  } catch {
    return null;
  }
}

function psBuildLineOffsets(text) {
  const offsets = [0];
  for (let i = 0; i < text.length; i += 1) {
    if (text.charCodeAt(i) === 10) offsets.push(i + 1);
  }
  return offsets;
}

function psLcToOffset(lineOffsets, line, col) {
  const lineIdx = Math.max(0, line - 1);
  if (lineIdx >= lineOffsets.length) return lineOffsets[lineOffsets.length - 1] || 0;
  return lineOffsets[lineIdx] + Math.max(0, col - 1);
}

function psFindTokenIndex(tokens, line, col) {
  for (let i = 0; i < tokens.length; i += 1) {
    if (tokens[i].line === line && tokens[i].col === col) return i;
  }
  return -1;
}

function psFindOpeningBraceIndex(tokens, startIdx) {
  for (let i = startIdx; i < tokens.length; i += 1) {
    const t = tokens[i];
    if (t.type === "sym" && t.value === "{") return i;
  }
  return -1;
}

function psFindMatchingBraceIndex(tokens, openIdx) {
  let depth = 0;
  for (let i = openIdx; i < tokens.length; i += 1) {
    const t = tokens[i];
    if (t.type === "sym" && t.value === "{") depth += 1;
    else if (t.type === "sym" && t.value === "}") {
      depth -= 1;
      if (depth === 0) return i;
    }
  }
  return -1;
}

function psCollectLocalsFromNode(node, out) {
  if (!node || typeof node !== "object") return;
  if (node.kind === "VarDecl") {
    out.push(node);
    return;
  }
  if (node.kind === "Block" && Array.isArray(node.stmts)) {
    for (const stmt of node.stmts) psCollectLocalsFromNode(stmt, out);
    return;
  }
  if (node.kind === "IfStmt") {
    psCollectLocalsFromNode(node.thenBranch, out);
    psCollectLocalsFromNode(node.elseBranch, out);
    return;
  }
  if (node.kind === "WhileStmt" || node.kind === "DoWhileStmt") {
    psCollectLocalsFromNode(node.body, out);
    return;
  }
  if (node.kind === "ForStmt") {
    psCollectLocalsFromNode(node.init, out);
    psCollectLocalsFromNode(node.body, out);
    return;
  }
  if (node.kind === "SwitchStmt") {
    if (Array.isArray(node.cases)) {
      for (const clause of node.cases) {
        if (Array.isArray(clause && clause.stmts)) {
          for (const stmt of clause.stmts) psCollectLocalsFromNode(stmt, out);
        }
      }
    }
    if (node.defaultCase && Array.isArray(node.defaultCase.stmts)) {
      for (const stmt of node.defaultCase.stmts) psCollectLocalsFromNode(stmt, out);
    }
    return;
  }
  if (node.kind === "TryStmt") {
    psCollectLocalsFromNode(node.tryBlock, out);
    if (Array.isArray(node.catches)) {
      for (const c of node.catches) psCollectLocalsFromNode(c && c.block, out);
    }
    psCollectLocalsFromNode(node.finallyBlock, out);
  }
}

function psTokenStartOffset(lineOffsets, token) {
  return psLcToOffset(lineOffsets, token.line, token.col);
}

function psTokenEndOffset(lineOffsets, token) {
  const start = psTokenStartOffset(lineOffsets, token);
  if (token.type === "id" || token.type === "kw" || token.type === "num" || token.type === "str") {
    return start + String(token.value || "").length;
  }
  if (token.type === "sym") return start + String(token.value || "").length;
  return start + 1;
}

function psFindTokenAtOrBeforeOffset(tokens, lineOffsets, offset) {
  let at = -1;
  for (let i = 0; i < tokens.length; i += 1) {
    const start = psTokenStartOffset(lineOffsets, tokens[i]);
    const end = psTokenEndOffset(lineOffsets, tokens[i]);
    if (offset >= start && offset < end) return i;
    if (start <= offset) at = i;
    else break;
  }
  return at;
}

function psResolvePrototypeField(model, protoName, fieldName) {
  let cur = model.prototypes.get(protoName);
  while (cur) {
    for (const f of cur.fields) {
      if (f.name === fieldName) return f;
    }
    cur = cur.parent ? model.prototypes.get(cur.parent) : null;
  }
  return null;
}

function psResolvePrototypeMethod(model, protoName, methodName) {
  let cur = model.prototypes.get(protoName);
  while (cur) {
    for (const m of cur.methods) {
      if (m.name === methodName) return m;
    }
    cur = cur.parent ? model.prototypes.get(cur.parent) : null;
  }
  return null;
}

function psGetEnclosingFunction(model, offset) {
  let best = null;
  for (const fn of model.functions) {
    if (offset < fn.startOffset || offset > fn.endOffset) continue;
    if (!best || fn.endOffset - fn.startOffset < best.endOffset - best.startOffset) best = fn;
  }
  return best;
}

function psResolveIdentifierType(model, offset, identifier, fnCtx) {
  if (fnCtx) {
    let localType = null;
    for (const local of fnCtx.locals) {
      if (local.name === identifier && local.declOffset <= offset) localType = local.typeName;
    }
    if (localType) return localType;
    if (fnCtx.params.has(identifier)) return fnCtx.params.get(identifier).typeName;
    if (fnCtx.prototypeName) {
      const f = psResolvePrototypeField(model, fnCtx.prototypeName, identifier);
      if (f) return f.typeName;
    }
  }
  if (model.prototypes.has(identifier)) return identifier;
  return null;
}

function psResolveIdentifierDecl(model, offset, identifier, fnCtx) {
  if (fnCtx) {
    let localDecl = null;
    for (const local of fnCtx.locals) {
      if (local.name === identifier && local.declOffset <= offset) localDecl = local.decl;
    }
    if (localDecl) return localDecl;
    if (fnCtx.params.has(identifier)) return fnCtx.params.get(identifier).decl;
    if (fnCtx.prototypeName) {
      const f = psResolvePrototypeField(model, fnCtx.prototypeName, identifier);
      if (f) return f.decl;
    }
  }
  const gf = model.globalFunctions.get(identifier);
  if (gf) return gf.decl;
  const p = model.prototypes.get(identifier);
  if (p) return p.decl;
  return null;
}

function psGetMemberCompletions(model, line, character) {
  const cursorOffset = psLcToOffset(model.lineOffsets, line + 1, character + 1);
  if (cursorOffset <= 0) return [];
  const dotIdx = psFindTokenAtOrBeforeOffset(model.tokens, model.lineOffsets, cursorOffset - 1);
  if (dotIdx < 0) return [];
  const dot = model.tokens[dotIdx];
  if (!(dot.type === "sym" && dot.value === ".")) return [];
  if (dotIdx === 0) return [];
  const left = model.tokens[dotIdx - 1];
  const isIdentifier = left.type === "id";
  const isSelf = left.type === "kw" && left.value === "self";
  if (!isIdentifier && !isSelf) return [];

  const fnCtx = psGetEnclosingFunction(model, cursorOffset);
  let resolvedType = null;
  if (isSelf) resolvedType = fnCtx && fnCtx.prototypeName ? fnCtx.prototypeName : null;
  else resolvedType = psResolveIdentifierType(model, cursorOffset, left.value, fnCtx);
  if (!resolvedType) return [];

  const completions = [];
  let cur = model.prototypes.get(resolvedType);
  const seen = new Set();
  while (cur) {
    for (const field of cur.fields) {
      if (seen.has(`f:${field.name}`)) continue;
      seen.add(`f:${field.name}`);
      completions.push({
        label: field.name,
        kind: "field",
        detail: `${resolvedType}.${field.name}: ${field.typeName}`,
      });
    }
    for (const method of cur.methods) {
      if (seen.has(`m:${method.name}`)) continue;
      seen.add(`m:${method.name}`);
      completions.push({
        label: method.name,
        kind: "method",
        detail: `${resolvedType}.${method.name}(${method.params.map((p) => p.label).join(", ")}): ${method.retType}`,
      });
    }
    cur = cur.parent ? model.prototypes.get(cur.parent) : null;
  }
  return completions;
}

function psGetHover(model, line, character) {
  const offset = psLcToOffset(model.lineOffsets, line + 1, character + 1);
  const idx = psFindTokenAtOrBeforeOffset(model.tokens, model.lineOffsets, offset);
  if (idx < 0) return null;
  const tok = model.tokens[idx];
  if (!(tok.type === "id" || (tok.type === "kw" && tok.value === "self"))) return null;

  const fnCtx = psGetEnclosingFunction(model, offset);
  if (tok.type === "kw" && tok.value === "self") {
    if (!fnCtx || !fnCtx.prototypeName) return null;
    return { contents: `self: ${fnCtx.prototypeName}` };
  }

  if (idx >= 2) {
    const dot = model.tokens[idx - 1];
    const left = model.tokens[idx - 2];
    if (dot.type === "sym" && dot.value === "." && (left.type === "id" || (left.type === "kw" && left.value === "self"))) {
      let leftType = null;
      if (left.type === "kw") leftType = fnCtx && fnCtx.prototypeName ? fnCtx.prototypeName : null;
      else leftType = psResolveIdentifierType(model, offset, left.value, fnCtx);
      if (leftType) {
        const f = psResolvePrototypeField(model, leftType, tok.value);
        if (f) return { contents: `${leftType}.${f.name}: ${f.typeName}` };
        const m = psResolvePrototypeMethod(model, leftType, tok.value);
        if (m) return { contents: `${leftType}.${m.name}(${m.params.map((p) => p.label).join(", ")}): ${m.retType}` };
      }
    }
  }

  const decl = psResolveIdentifierDecl(model, offset, tok.value, fnCtx);
  if (!decl) return null;
  if (decl.kind === "Param") {
    return { contents: `param ${decl.name}: ${psTypeName(decl.type) || "unknown"}` };
  }
  if (decl.kind === "VarDecl") {
    return { contents: `local ${decl.name}: ${psTypeName(decl.declaredType) || "unknown"}` };
  }
  if (decl.kind === "FunctionDecl") {
    const params = Array.isArray(decl.params) ? decl.params.map((p) => `${psTypeName(p.type) || "unknown"} ${p.name}`).join(", ") : "";
    return { contents: `function ${decl.name}(${params}): ${psTypeName(decl.retType) || "void"}` };
  }
  if (decl.kind === "PrototypeDecl") {
    return { contents: `prototype ${decl.name}` };
  }
  if (decl.kind === "FieldDecl") {
    return { contents: `field ${decl.name}: ${psTypeName(decl.type) || "unknown"}` };
  }
  return null;
}

function psGetDefinition(model, line, character) {
  const offset = psLcToOffset(model.lineOffsets, line + 1, character + 1);
  const idx = psFindTokenAtOrBeforeOffset(model.tokens, model.lineOffsets, offset);
  if (idx < 0) return null;
  const tok = model.tokens[idx];
  if (!(tok.type === "id" || (tok.type === "kw" && tok.value === "self"))) return null;
  const fnCtx = psGetEnclosingFunction(model, offset);

  if (tok.type === "kw" && tok.value === "self") {
    if (!fnCtx || !fnCtx.prototypeName) return null;
    const proto = model.prototypes.get(fnCtx.prototypeName);
    if (!proto || !proto.decl) return null;
    return { line: Math.max(0, (proto.decl.line || 1) - 1), character: Math.max(0, (proto.decl.col || 1) - 1) };
  }

  if (idx >= 2) {
    const dot = model.tokens[idx - 1];
    const left = model.tokens[idx - 2];
    if (dot.type === "sym" && dot.value === "." && (left.type === "id" || (left.type === "kw" && left.value === "self"))) {
      let leftType = null;
      if (left.type === "kw") leftType = fnCtx && fnCtx.prototypeName ? fnCtx.prototypeName : null;
      else leftType = psResolveIdentifierType(model, offset, left.value, fnCtx);
      if (leftType) {
        const f = psResolvePrototypeField(model, leftType, tok.value);
        if (f && f.decl) return { line: Math.max(0, (f.decl.line || 1) - 1), character: Math.max(0, (f.decl.col || 1) - 1) };
        const m = psResolvePrototypeMethod(model, leftType, tok.value);
        if (m && m.decl) return { line: Math.max(0, (m.decl.line || 1) - 1), character: Math.max(0, (m.decl.col || 1) - 1) };
      }
    }
  }

  const decl = psResolveIdentifierDecl(model, offset, tok.value, fnCtx);
  if (!decl) return null;
  return { line: Math.max(0, (decl.line || 1) - 1), character: Math.max(0, (decl.col || 1) - 1) };
}

function psGetSignature(model, line, character) {
  const offset = psLcToOffset(model.lineOffsets, line + 1, character + 1);
  const idx = psFindTokenAtOrBeforeOffset(model.tokens, model.lineOffsets, offset);
  if (idx < 0) return null;

  let depth = 0;
  let openIdx = -1;
  for (let i = idx; i >= 0; i -= 1) {
    const t = model.tokens[i];
    if (t.type !== "sym") continue;
    if (t.value === ")") depth += 1;
    else if (t.value === "(") {
      if (depth === 0) {
        openIdx = i;
        break;
      }
      depth -= 1;
    }
  }
  if (openIdx < 1) return null;

  let activeParameter = 0;
  let argDepth = 0;
  for (let i = openIdx + 1; i <= idx; i += 1) {
    const t = model.tokens[i];
    if (!t || t.type !== "sym") continue;
    if (t.value === "(") argDepth += 1;
    else if (t.value === ")") {
      if (argDepth > 0) argDepth -= 1;
    } else if (t.value === "," && argDepth === 0) {
      activeParameter += 1;
    }
  }

  const before = model.tokens[openIdx - 1];
  const fnCtx = psGetEnclosingFunction(model, offset);
  if (!before) return null;

  if (before.type === "id") {
    if (openIdx >= 3) {
      const dot = model.tokens[openIdx - 2];
      const left = model.tokens[openIdx - 3];
      if (dot && dot.type === "sym" && dot.value === "." && left && (left.type === "id" || (left.type === "kw" && left.value === "self"))) {
        let leftType = null;
        if (left.type === "kw") leftType = fnCtx && fnCtx.prototypeName ? fnCtx.prototypeName : null;
        else leftType = psResolveIdentifierType(model, offset, left.value, fnCtx);
        if (!leftType) return null;
        const m = psResolvePrototypeMethod(model, leftType, before.value);
        if (!m) return null;
        return {
          label: `${leftType}.${m.name}(${m.params.map((p) => p.label).join(", ")}): ${m.retType}`,
          parameters: m.params.map((p) => p.label),
          activeParameter,
        };
      }
    }

    const gf = model.globalFunctions.get(before.value);
    if (!gf) return null;
    return {
      label: `${gf.name}(${gf.params.map((p) => p.label).join(", ")}): ${gf.retType}`,
      parameters: gf.params.map((p) => p.label),
      activeParameter,
    };
  }
  return null;
}

function psBuildIdentityLineMap(text) {
  const lines = text.split(/\n/);
  const preToOrigLine = [0];
  const origToPreLine = [0];
  for (let i = 1; i <= lines.length; i += 1) {
    preToOrigLine[i] = i;
    origToPreLine[i] = i;
  }
  return { preToOrigLine, origToPreLine };
}

function psBuildOrigToPre(preToOrigLine) {
  const origToPreLine = [0];
  for (let pre = 1; pre < preToOrigLine.length; pre += 1) {
    const orig = preToOrigLine[pre];
    if (!orig || orig <= 0) continue;
    if (!origToPreLine[orig]) origToPreLine[orig] = pre;
  }
  return origToPreLine;
}

let psMcppWasm = null;

function psLoadMcppWasm(options = {}) {
  if (options && options.mcppModule) {
    const mod = options.mcppModule;
    if (typeof mod.preprocess !== "function") {
      throw new FrontendError(diag("<preprocess>", 1, 1, "E0003", "PREPROCESS_ERROR", "invalid mcpp module API"));
    }
    return mod;
  }
  if (psMcppWasm) return psMcppWasm;
  const wrapperPath = path.resolve(__dirname, "../tools/mcpp-wasm/out/mcpp_node.js");
  if (!fs.existsSync(wrapperPath)) {
    throw new FrontendError(
      diag("<preprocess>", 1, 1, "E0003", "PREPROCESS_ERROR", `missing mcpp wasm wrapper: ${wrapperPath}`)
    );
  }
  psMcppWasm = require(wrapperPath);
  if (!psMcppWasm || typeof psMcppWasm.preprocess !== "function") {
    throw new FrontendError(diag("<preprocess>", 1, 1, "E0003", "PREPROCESS_ERROR", "invalid mcpp wasm wrapper API"));
  }
  return psMcppWasm;
}

function psPreprocessSource(file, src, options = {}) {
  const usePreprocessor = options.usePreprocessor !== false;
  if (!usePreprocessor) {
    const map = psBuildIdentityLineMap(src);
    return { code: src, preToOrigLine: map.preToOrigLine, origToPreLine: map.origToPreLine };
  }

  const mcpp = psLoadMcppWasm(options);
  try {
    const result = mcpp.preprocess(src, { file: file || "<input>" });
    const code = typeof result?.code === "string" ? result.code : "";
    const preToOrigLine = Array.isArray(result?.mapping?.preToOrigLine) ? result.mapping.preToOrigLine : psBuildIdentityLineMap(code).preToOrigLine;
    const origToPreLine = Array.isArray(result?.mapping?.origToPreLine)
      ? result.mapping.origToPreLine
      : psBuildOrigToPre(preToOrigLine);
    return { code, preToOrigLine, origToPreLine };
  } catch (err) {
    const msg = err && err.message ? err.message : String(err);
    throw new FrontendError(diag(file || "<input>", 1, 1, "E0003", "PREPROCESS_ERROR", msg));
  }
}

function psMapOrigToPre(lineZero, lineMap) {
  const orig = lineZero + 1;
  const pre = lineMap.origToPreLine[orig];
  if (!pre || pre <= 0) return null;
  return pre - 1;
}

function psMapPreToOrig(lineZero, lineMap) {
  const pre = lineZero + 1;
  const orig = lineMap.preToOrigLine[pre];
  if (!orig || orig <= 0) return null;
  return orig - 1;
}

function buildSemanticModel(file, src, options = {}) {
  const preprocessed = psPreprocessSource(file, src, options);
  const parsed = parseAndAnalyze(file, preprocessed.code);
  const tokens = Array.isArray(parsed.tokens) ? parsed.tokens : [];
  const ast = parsed.ast || { decls: [] };
  const lineOffsets = psBuildLineOffsets(preprocessed.code);

  const prototypes = new Map();
  const globalFunctions = new Map();
  const functions = [];

  const decls = Array.isArray(ast.decls) ? ast.decls : [];
  for (const decl of decls) {
    if (decl && decl.kind === "PrototypeDecl" && typeof decl.name === "string") {
      const info = {
        name: decl.name,
        parent: decl.parent || null,
        decl,
        fields: [],
        methods: [],
      };
      if (Array.isArray(decl.fields)) {
        for (const field of decl.fields) {
          if (!field || typeof field.name !== "string") continue;
          info.fields.push({
            name: field.name,
            typeName: psTypeName(field.type) || "unknown",
            decl: field,
          });
        }
      }
      if (Array.isArray(decl.methods)) {
        for (const method of decl.methods) {
          if (!method || typeof method.name !== "string") continue;
          const params = Array.isArray(method.params)
            ? method.params
                .filter((p) => p && typeof p.name === "string")
                .map((p) => ({
                  name: p.name,
                  typeName: psTypeName(p.type) || "unknown",
                  label: `${psTypeName(p.type) || "unknown"} ${p.name}`,
                  decl: p,
                }))
            : [];
          info.methods.push({
            name: method.name,
            params,
            retType: psTypeName(method.retType) || "void",
            decl: method,
          });
        }
      }
      prototypes.set(decl.name, info);

      if (Array.isArray(decl.methods)) {
        for (const method of decl.methods) {
          if (!method || method.kind !== "FunctionDecl") continue;
          const startIdx = psFindTokenIndex(tokens, method.line, method.col);
          if (startIdx < 0) continue;
          const openIdx = psFindOpeningBraceIndex(tokens, startIdx);
          if (openIdx < 0) continue;
          const closeIdx = psFindMatchingBraceIndex(tokens, openIdx);
          if (closeIdx < 0) continue;
          const fnCtx = {
            name: method.name,
            prototypeName: decl.name,
            startOffset: psLcToOffset(lineOffsets, tokens[openIdx].line, tokens[openIdx].col),
            endOffset: psLcToOffset(lineOffsets, tokens[closeIdx].line, tokens[closeIdx].col + 1),
            params: new Map(),
            locals: [],
          };
          if (Array.isArray(method.params)) {
            for (const param of method.params) {
              if (!param || typeof param.name !== "string") continue;
              fnCtx.params.set(param.name, { typeName: psTypeName(param.type) || "unknown", decl: param });
            }
          }
          const vars = [];
          psCollectLocalsFromNode(method.body, vars);
          for (const v of vars) {
            if (!v || typeof v.name !== "string") continue;
            const typeName = psTypeName(v.declaredType);
            if (!typeName) continue;
            fnCtx.locals.push({
              name: v.name,
              typeName,
              declOffset: psLcToOffset(lineOffsets, v.line || 1, v.col || 1),
              decl: v,
            });
          }
          fnCtx.locals.sort((a, b) => a.declOffset - b.declOffset);
          functions.push(fnCtx);
        }
      }
      continue;
    }

    if (decl && decl.kind === "FunctionDecl" && typeof decl.name === "string") {
      const params = Array.isArray(decl.params)
        ? decl.params
            .filter((p) => p && typeof p.name === "string")
            .map((p) => ({
              name: p.name,
              typeName: psTypeName(p.type) || "unknown",
              label: `${psTypeName(p.type) || "unknown"} ${p.name}`,
              decl: p,
            }))
        : [];
      globalFunctions.set(decl.name, {
        name: decl.name,
        params,
        retType: psTypeName(decl.retType) || "void",
        decl,
      });

      const startIdx = psFindTokenIndex(tokens, decl.line, decl.col);
      if (startIdx >= 0) {
        const openIdx = psFindOpeningBraceIndex(tokens, startIdx);
        const closeIdx = openIdx >= 0 ? psFindMatchingBraceIndex(tokens, openIdx) : -1;
        if (openIdx >= 0 && closeIdx >= 0) {
          const fnCtx = {
            name: decl.name,
            prototypeName: null,
            startOffset: psLcToOffset(lineOffsets, tokens[openIdx].line, tokens[openIdx].col),
            endOffset: psLcToOffset(lineOffsets, tokens[closeIdx].line, tokens[closeIdx].col + 1),
            params: new Map(),
            locals: [],
          };
          for (const p of params) fnCtx.params.set(p.name, { typeName: p.typeName, decl: p.decl });
          const vars = [];
          psCollectLocalsFromNode(decl.body, vars);
          for (const v of vars) {
            if (!v || typeof v.name !== "string") continue;
            const typeName = psTypeName(v.declaredType);
            if (!typeName) continue;
            fnCtx.locals.push({
              name: v.name,
              typeName,
              declOffset: psLcToOffset(lineOffsets, v.line || 1, v.col || 1),
              decl: v,
            });
          }
          fnCtx.locals.sort((a, b) => a.declOffset - b.declOffset);
          functions.push(fnCtx);
        }
      }
    }
  }

  const internal = { lineOffsets, tokens, prototypes, globalFunctions, functions, options };
  const lineMap = { preToOrigLine: preprocessed.preToOrigLine, origToPreLine: preprocessed.origToPreLine };
  const diagnostics = Array.isArray(parsed.diags)
    ? parsed.diags.map((d) => {
        const mapped = psMapPreToOrig(Math.max(0, (d.line || 1) - 1), lineMap);
        if (mapped === null) return d;
        return { ...d, line: mapped + 1, column: d.column || 1, col: d.column || 1 };
      })
    : [];

  return {
    diagnostics,
    queries: {
      completionsAt(line, character) {
        const mappedLine = psMapOrigToPre(line, lineMap);
        if (mappedLine === null) return [];
        return psGetMemberCompletions(internal, mappedLine, character);
      },
      hoverAt(line, character) {
        const mappedLine = psMapOrigToPre(line, lineMap);
        if (mappedLine === null) return null;
        return psGetHover(internal, mappedLine, character);
      },
      definitionAt(line, character) {
        const mappedLine = psMapOrigToPre(line, lineMap);
        if (mappedLine === null) return null;
        const def = psGetDefinition(internal, mappedLine, character);
        if (!def) return null;
        const outLine = psMapPreToOrig(def.line, lineMap);
        if (outLine === null) return null;
        return { line: outLine, character: def.character };
      },
      signatureAt(line, character) {
        const mappedLine = psMapOrigToPre(line, lineMap);
        if (mappedLine === null) return null;
        return psGetSignature(internal, mappedLine, character);
      },
    },
  };
}

module.exports = {
  check,
  parseAndAnalyze,
  parseOnly,
  buildSemanticModel,
  formatDiag,
  FrontendError,
};
