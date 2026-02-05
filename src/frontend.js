"use strict";

const KEYWORDS = new Set([
  "prototype",
  "function",
  "var",
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
]);

class FrontendError extends Error {
  constructor(diag) {
    super(diag.message);
    this.diag = diag;
  }
}

function diag(file, line, col, code, category, message) {
  return { file, line, col, code, category, message };
}

function formatDiag(d) {
  return `${d.file}:${d.line}:${d.col} ${d.code} ${d.category}: ${d.message}`;
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
            s += this.advance();
            if (!this.eof()) s += this.advance();
            continue;
          }
          s += this.advance();
        }
        if (this.eof()) {
          throw new FrontendError(
            diag(this.file, line, col, "E1002", "PARSE_UNCLOSED_BLOCK", "Unclosed string literal")
          );
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
    while (!this.at("eof")) {
      if (this.at("kw", "function")) decls.push(this.parseFunctionDecl());
      else {
        const tok = this.t();
        throw new FrontendError(
          diag(this.file, tok.line, tok.col, "E1001", "PARSE_UNEXPECTED_TOKEN", "Top-level declaration expected")
        );
      }
    }
    return { kind: "Program", decls };
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
    if (this.looksLikeTypedVarDecl()) return this.parseVarDeclStmt();
    if (this.at("kw", "for")) return this.parseForStmt();
    if (this.at("kw", "switch")) return this.parseSwitchStmt();
    if (this.at("kw", "return")) return this.parseReturnStmt();
    if (this.at("kw", "break")) return this.parseSimpleStmt("BreakStmt", "break");
    if (this.at("kw", "continue")) return this.parseSimpleStmt("ContinueStmt", "continue");
    if (this.at("kw", "throw")) return this.parseThrowStmt();
    if (this.looksLikeAssignStmt()) return this.parseAssignStmt(true);
    return this.parseExprStmt();
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
      diag(this.file, tok.line, tok.col, "E1001", "PARSE_UNEXPECTED_TOKEN", `unexpected token '${tok.value}'`)
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

function sameType(a, b) {
  return typeToString(a) === typeToString(b);
}

class Analyzer {
  constructor(file, ast) {
    this.file = file;
    this.ast = ast;
    this.functions = new Map();
    this.diags = [];
  }

  addDiag(node, code, category, message) {
    this.diags.push(diag(this.file, node.line || 1, node.col || 1, code, category, message));
  }

  analyze() {
    for (const d of this.ast.decls) {
      if (d.kind === "FunctionDecl") this.functions.set(d.name, d);
    }
    for (const d of this.ast.decls) {
      if (d.kind === "FunctionDecl") this.analyzeFunction(d);
    }
    return this.diags;
  }

  analyzeFunction(fn) {
    const scope = new Scope(null);
    for (const p of fn.params) {
      scope.define(p.name, p.type, true);
    }
    this.analyzeBlock(fn.body, scope, fn);
  }

  analyzeBlock(block, scope, fn) {
    const local = new Scope(scope);
    for (const s of block.stmts) this.analyzeStmt(s, local, fn);
  }

  analyzeStmt(stmt, scope, fn) {
    switch (stmt.kind) {
      case "VarDecl": {
        let t = stmt.declaredType;
        if (stmt.init) {
          const initType = this.typeOfExpr(stmt.init, scope);
          const emptyMapInit =
            !!t &&
            stmt.init.kind === "MapLiteral" &&
            stmt.init.pairs.length === 0 &&
            typeToString(t).startsWith("map<");
          if (t && initType && !sameType(t, initType) && !emptyMapInit) {
            this.addDiag(stmt, "E3001", "TYPE_MISMATCH_ASSIGNMENT", `cannot assign ${typeToString(initType)} to ${typeToString(t)}`);
          }
          if (!t) t = initType;
        }
        scope.define(stmt.name, t || { kind: "PrimitiveType", name: "void" }, true);
        break;
      }
      case "AssignStmt": {
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
        if (lhsType && rhsType && !sameType(lhsType, rhsType) && !emptyMapAssign) {
          this.addDiag(stmt, "E3001", "TYPE_MISMATCH_ASSIGNMENT", `cannot assign ${typeToString(rhsType)} to ${typeToString(lhsType)}`);
        }
        break;
      }
      case "ExprStmt":
        this.typeOfExpr(stmt.expr, scope);
        break;
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
      case "ReturnStmt":
        if (stmt.expr) this.typeOfExpr(stmt.expr, scope);
        break;
      case "ThrowStmt":
        this.typeOfExpr(stmt.expr, scope);
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
        this.addDiag(expr, "E2001", "UNRESOLVED_NAME", `unknown identifier '${expr.name}'`);
        return null;
      }
      return s.type;
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
      case "Identifier": {
        const s = scope.lookup(expr.name);
        if (!s) {
          this.addDiag(expr, "E2001", "UNRESOLVED_NAME", `unknown identifier '${expr.name}'`);
          return null;
        }
        return s.type;
      }
      case "UnaryExpr": {
        const t = this.typeOfExpr(expr.expr, scope);
        if (!t) return null;
        if (expr.op === "-" || expr.op === "++" || expr.op === "--") return t;
        if (expr.op === "!") return { kind: "PrimitiveType", name: "bool" };
        return t;
      }
      case "BinaryExpr": {
        const lt = this.typeOfExpr(expr.left, scope);
        const rt = this.typeOfExpr(expr.right, scope);
        if (!lt || !rt) return null;
        if (!sameType(lt, rt)) {
          this.addDiag(expr, "E3001", "TYPE_MISMATCH_ASSIGNMENT", `incompatible operands ${typeToString(lt)} and ${typeToString(rt)}`);
          return lt;
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
        return null;
      case "PostfixExpr":
        return this.typeOfExpr(expr.expr, scope);
      case "ListLiteral":
        if (expr.items.length === 0) return { kind: "GenericType", name: "list", args: [{ kind: "PrimitiveType", name: "void" }] };
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

  typeOfCall(expr, scope) {
    if (expr.callee.kind !== "Identifier") return null;
    const name = expr.callee.name;
    const fn = this.functions.get(name);
    if (!fn) return null;
    const variadic = fn.params.find((p) => p.variadic);
    if (!variadic) {
      if (expr.args.length !== fn.params.length) {
        this.addDiag(expr, "E3001", "TYPE_MISMATCH_ASSIGNMENT", `arity mismatch for '${name}'`);
      }
    } else {
      const fixed = fn.params.filter((p) => !p.variadic).length;
      if (expr.args.length <= fixed) {
        this.addDiag(expr, "E3002", "VARIADIC_EMPTY_CALL", "variadic argument list must be non-empty");
      }
    }
    return fn.retType;
  }
}

class Scope {
  constructor(parent) {
    this.parent = parent;
    this.syms = new Map();
  }
  define(name, type, initialized) {
    this.syms.set(name, { type, initialized });
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

module.exports = {
  check,
  parseAndAnalyze,
  parseOnly,
  formatDiag,
  FrontendError,
};
