# ProtoScript2 — Boundary Definition

## 1. Core Language

Includes reserved keywords, core types, syntax constructs, operators, and core diagnostics.

- Keywords: `prototype`, `sealed`, `function`, `var`, `const`, `internal`, `group`, `int`, `float`, `bool`, `byte`, `glyph`, `string`, `list`, `map`, `slice`, `view`, `void`, `if`, `else`, `for`, `of`, `in`, `while`, `do`, `switch`, `case`, `default`, `break`, `continue`, `return`, `try`, `catch`, `finally`, `throw`, `true`, `false`, `self`, `import`, `as`
- Core types: `bool`, `byte`, `int`, `float`, `glyph`, `string`, `void`, `list`, `map`, `slice`, `view`
- Syntax constructs: `import_decl`, `prototype_decl`, `sealed_prototype_decl`, `group_decl`, `function_decl`, `var_decl`, `const_decl`, `if_stmt`, `while_stmt`, `do_while_stmt`, `for_stmt`, `switch_stmt`, `try_stmt`, `catch_clause`, `finally_clause`, `throw_stmt`, `return_stmt`, `break_stmt`, `continue_stmt`, `assign_expr`, `conditional_expr`, `cast_expr`, `call_expr`, `member_expr`, `index_expr`, `list_literal`, `map_literal`
- Operators/tokens: `==` `!=` `<=` `>=` `&&` `||` `<<` `>>` `++` `--` `+=` `-=` `*=` `/=` `{` `}` `(` `)` `[` `]` `;` `,` `:` `.` `?` `+` `-` `*` `/` `%` `&` `|` `^` `~` `!` `=` `<` `>` `...`

## 2. Standard Library

Official modules from registry (non-test):

- `Math`
- `Debug`
- `Io`
- `Fs`
- `Sys`
- `JSON`
- `Time`
- `TimeCivil`
- `RegExp`

Exposed types include: `TextFile`, `BinaryFile`, `JSONValue`, `CivilDateTime`, `PathInfo`, `PathEntry`, `Dir`, `Walker`, `RegExp`, `RegExpMatch`.

## 3. Runtime-only Constructs

Symbols present in runtime implementation but not part of standard registry surface:

- `ProcessEvent`
- `ProcessResult`
- `ProcessCreationException`
- `ProcessExecutionException`
- `ProcessPermissionException`
- `InvalidExecutableException`
- `EnvironmentAccessException`
- `InvalidEnvironmentNameException`
- `NotADirectoryException`
- `NotAFileException`
- `DirectoryNotEmptyException`

## 4. Test-only Constructs

Registry modules reserved to tests:

- `test.simple`
- `test.utf8`
- `test.env`
- `test.throw`
- `test.noinit`
- `test.badver`
- `test.nosym`
- `test.missing`
- `test.invalid`

## 5. Classification Rules

- `CORE`: lexer/parser/type system/operators/diagnostics required by language frontends.
- `STDLIB`: symbols exported by non-test entries in `modules/registry.json` and their documented built-in module types.
- `RUNTIME_ONLY`: symbols injected by runtime implementation only (not exported as standard registry module symbols).
- `TEST_ONLY`: symbols under `test.*` modules in registry and test harness modules.

Ambiguous cases are classified by repository source precedence:

1. Explicit module registry export (STDLIB/TEST_ONLY).
2. Frontend lexer/parser/type tables (CORE).
3. Runtime-only declarations not in registry/spec surface (RUNTIME_ONLY).

## Verification Notes

- Files analyzed: `src/frontend.js`, `c/frontend.c`, `src/runtime.js`, `modules/registry.json`, `c/runtime/ps_errors.c`, `tests/modules_src/*`, `tests/**/*.expect.json`.
- Method: static extraction by symbol scan with explicit location capture.
- Ambiguities: some symbols exist in runtime Node only or are injected in frontend C without registry entries.
- Limits: location references for registry-derived function/constant names are tied to registry declarations.
