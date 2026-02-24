# ProtoScript2 Lexical Cartography

## 1. Reserved Keywords

Source principale: `src/frontend.js` (`KEYWORDS`).

| Keyword |
|---|
| `prototype` |
| `sealed` |
| `function` |
| `var` |
| `const` |
| `internal` |
| `group` |
| `int` |
| `float` |
| `bool` |
| `byte` |
| `glyph` |
| `string` |
| `list` |
| `map` |
| `slice` |
| `view` |
| `void` |
| `if` |
| `else` |
| `for` |
| `of` |
| `in` |
| `while` |
| `do` |
| `switch` |
| `case` |
| `default` |
| `break` |
| `continue` |
| `return` |
| `try` |
| `catch` |
| `finally` |
| `throw` |
| `true` |
| `false` |
| `self` |
| `super` |
| `import` |
| `as` |

## 2. Fundamental Types

### 2.1 Scalar Fundamental Types

- `bool`
- `byte`
- `int`
- `float`
- `glyph`
- `string`
- `void` (retour)

### 2.2 Generic/Container Types

- `list<T>`
- `map<K,V>`
- `slice<T>`
- `view<T>`

### 2.3 Named Built-in Types Used by Frontend/Runtime

- `Exception`, `RuntimeException`
- `CivilDateTime`
- `TextFile`, `BinaryFile`
- `PathInfo`, `PathEntry`, `Dir`, `Walker`
- `JSONValue`
- `RegExp`, `RegExpMatch`
- `ProcessResult`, `ProcessEvent` (runtime Node)
- `Self` (type contextuel normatif, spécialisé au receveur pour `clone()`)

### 2.4 Numeric Conversions (explicit cast)

Source: `src/frontend.js` and `c/frontend.c`.

- Cast syntax: `(int)expr`, `(float)expr`, `(byte)expr`.
- Numeric casts are checked statically for representability of literals where applicable.
- Runtime checks exist for out-of-range conversions (e.g. byte range).

### 2.5 Operators by Type (semantic view)

- Arithmetic: `+ - * / %` on numeric types (`int`, `byte`, `float`) with type constraints.
- Bitwise: `& | ^ << >> ~` on integer domain (`int`/`byte`, with shift range runtime checks).
- Logical: `&& || !` on `bool`.
- Comparison: `== != < <= > >=` with compatibility checks.
- Ternary: `cond ? a : b` requires `cond: bool` and compatible branch types.
- Assignment ops: `= += -= *= /=` with assignability rules.
- Increment/decrement: prefix/postfix `++ --` on assignable numeric targets.

## 3. Built-in Methods by Type

Source principale: `c/frontend.c` (`check_method_arity`, `method_ret_type`) and runtime parity in `src/runtime.js`.

### 3.1 `int`

- `toByte() -> byte`
- `toFloat() -> float`
- `toString() -> string`
- `toBytes() -> list<byte>`
- `abs() -> int`
- `sign() -> int`

### 3.2 `byte`

- `toInt() -> int`
- `toFloat() -> float`
- `toString() -> string`

### 3.3 `float`

- `toInt() -> int`
- `toString() -> string`
- `toBytes() -> list<byte>`
- `abs() -> float`
- `isNaN() -> bool`
- `isInfinite() -> bool`
- `isFinite() -> bool`

### 3.4 `glyph`

- `toString() -> string`
- `toInt() -> int`
- `toUtf8Bytes() -> list<byte>`
- `isLetter() -> bool`
- `isDigit() -> bool`
- `isWhitespace() -> bool`
- `isUpper() -> bool`
- `isLower() -> bool`
- `toUpper() -> glyph`
- `toLower() -> glyph`

### 3.5 `string`

- `length() -> int`
- `isEmpty() -> bool`
- `toString() -> string`
- `toInt() -> int`
- `toFloat() -> float`
- `concat(string) -> string`
- `subString(int,int) -> string`
- `indexOf(string) -> int`
- `contains(string) -> bool`
- `lastIndexOf(string) -> int`
- `startsWith(string) -> bool`
- `endsWith(string) -> bool`
- `split(string) -> list<string>`
- `trim() -> string`
- `trimStart() -> string`
- `trimEnd() -> string`
- `replace(string,string) -> string`
- `replaceAll(string,string) -> string`
- `glyphAt(int) -> glyph`
- `repeat(int) -> string`
- `padStart(int,string) -> string`
- `padEnd(int,string) -> string`
- `toUpper() -> string`
- `toLower() -> string`
- `toUtf8Bytes() -> list<byte>`

### 3.6 `list<T>`

- `length() -> int`
- `isEmpty() -> bool`
- `push(T) -> int`
- `pop() -> T` (runtime/static checks on empty list)
- `contains(T) -> bool`
- `sort() -> int`
- `reverse() -> int`
- `view(int,int) -> view<T>`
- `slice(int,int) -> slice<T>`
- Specializations:
- `list<byte>.toUtf8String() -> string`
- `list<string>.join(string) -> string`
- `list<string>.concat() -> string`

### 3.7 `slice<T>`

- `length() -> int`
- `isEmpty() -> bool`
- `slice(int,int) -> slice<T>`

### 3.8 `view<T>`

- `length() -> int`
- `isEmpty() -> bool`
- `view(int,int) -> view<T>`

### 3.9 `map<K,V>`

- `length() -> int`
- `isEmpty() -> bool`
- `containsKey(K) -> bool`
- `remove(K) -> bool`
- `keys() -> list<K>`
- `values() -> list<V>`

### 3.10 `TextFile`

- `read(int) -> string`
- `write(string) -> void`
- `seek(int) -> void`
- `tell() -> int`
- `size() -> int`
- `name() -> string`
- `close() -> void`

### 3.11 `BinaryFile`

- `read(int) -> list<byte>`
- `write(list<byte>) -> void`
- `seek(int) -> void`
- `tell() -> int`
- `size() -> int`
- `name() -> string`
- `close() -> void`

### 3.12 `Dir`

- `hasNext() -> bool`
- `next() -> string`
- `close() -> void`
- `reset() -> void`

### 3.13 `Walker`

- `hasNext() -> bool`
- `next() -> PathEntry`
- `close() -> void`

### 3.14 `JSONValue`

- `isNull() -> bool`
- `isBool() -> bool`
- `isNumber() -> bool`
- `isString() -> bool`
- `isArray() -> bool`
- `isObject() -> bool`
- `asBool() -> bool`
- `asNumber() -> float`
- `asString() -> string`
- `asArray() -> list<JSONValue>`
- `asObject() -> map<string,JSONValue>`

### 3.15 `RegExp`

- `test(string,int) -> bool`
- `find(string,int) -> RegExpMatch`
- `findAll(string,int,int) -> list<RegExpMatch>`
- `replaceFirst(string,string,int) -> string`
- `replaceAll(string,string,int,int) -> string`
- `split(string,int,int) -> list<string>`
- `pattern() -> string`
- `flags() -> string`

## 4. Native Modules

Source: `modules/registry.json`, `c/modules/*.c`, `tests/modules_src/*.c`, `scripts/build_modules.sh`.

### 4.1 Standard/Native Modules in Registry

- `Math`
- `Debug`
- `Io`
- `Fs`
- `Sys`
- `JSON`
- `Time`
- `TimeCivil`
- `RegExp`

### 4.2 Test Modules in Registry (test harness)

- `test.simple`
- `test.utf8`
- `test.env`
- `test.throw`
- `test.noinit`
- `test.badver`
- `test.nosym`
- `test.missing`
- `test.invalid`

### 4.3 Module Functions (standard modules)

#### `Math`

- `abs, min, max, floor, ceil, round, sqrt, pow`
- `sin, cos, tan, asin, acos, atan, atan2`
- `sinh, cosh, tanh, asinh, acosh, atanh`
- `exp, expm1, log, log1p, log2, log10, cbrt, hypot, trunc, sign, fround, clz32, imul, random`

Constants:

- `PI, E, LN2, LN10, LOG2E, LOG10E, SQRT1_2, SQRT2`
- `INT_MAX, INT_MIN, INT_SIZE`
- `FLOAT_DIG, FLOAT_EPSILON, FLOAT_MIN, FLOAT_MAX`

#### `Debug`

- `dump(any) -> void`

#### `Io`

- `openText(path,mode) -> TextFile`
- `openBinary(path,mode) -> BinaryFile`
- `tempPath() -> string`
- `print(string) -> void`
- `printLine(string) -> void`

Constants:

- `EOL`, `stdin`, `stdout`, `stderr`

#### `Fs`

- `exists, isFile, isDir, isSymlink, isReadable, isWritable, isExecutable`
- `size, mkdir, rmdir, rm, cp, mv, chmod, cwd, cd`
- `pathInfo, openDir, walk`

#### `Sys`

- `hasEnv(name) -> bool`
- `env(name) -> string`
- `execute(program,args,input,captureStdout,captureStderr) -> ProcessResult`

#### `JSON`

- `encode, decode, isValid`
- `null, bool, number, string, array, object`

#### `Time`

- `nowEpochMillis, nowMonotonicNanos, sleepMillis`

#### `TimeCivil`

- `fromEpochUTC, toEpochUTC, fromEpoch, toEpoch`
- `isDST, offsetSeconds, standardOffsetSeconds`
- `dayOfWeek, dayOfYear, weekOfYearISO, weekYearISO`
- `isLeapYear, daysInMonth`
- `parseISO8601, formatISO8601`

Constants:

- `DST_EARLIER, DST_LATER, DST_ERROR`

#### `RegExp`

- `compile, test, find, findAll, replaceFirst, replaceAll, split, pattern, flags`

## 5. Exceptions and Error Codes

### 5.1 Built-in Exception Prototypes (declared/injected)

From `c/frontend.c` and `src/runtime.js`:

- Root: `Exception`
- Runtime root: `RuntimeException`

Time-related:

- `DSTAmbiguousTimeException`
- `DSTNonExistentTimeException`
- `InvalidTimeZoneException`
- `InvalidDateException`
- `InvalidISOFormatException`

IO/FS-related:

- `InvalidModeException`
- `FileOpenException`
- `FileNotFoundException`
- `PermissionDeniedException`
- `InvalidPathException`
- `FileClosedException`
- `InvalidGlyphPositionException`
- `ReadFailureException`
- `WriteFailureException`
- `Utf8DecodeException`
- `StandardStreamCloseException`
- `IOException`
- `NotADirectoryException` (runtime Node set)
- `NotAFileException` (runtime Node set)
- `DirectoryNotEmptyException` (runtime Node set)

Runtime Node also declares additional exception prototypes:

- `ProcessCreationException`
- `ProcessExecutionException`
- `ProcessPermissionException`
- `InvalidExecutableException`
- `EnvironmentAccessException`
- `InvalidEnvironmentNameException`

### 5.2 Static/Frontend Error Codes

Detected in `src/frontend.js` and `c/frontend.c`:

- `E0001`, `E0002`, `E0003`
- `E1001`, `E1002`, `E1003`
- `E2001`, `E2002`, `E2003`, `E2004`
- `E3001`, `E3003`, `E3004`, `E3005`, `E3006`, `E3007`
- `E3120`, `E3121`, `E3122`
- `E3130`, `E3131`, `E3140`, `E3150`, `E3151`
- `E3200`, `E3201`

Representative categories observed in tests:

- `PARSE_UNEXPECTED_TOKEN`, `PARSE_UNCLOSED_BLOCK`
- `ARITY_MISMATCH`
- `UNRESOLVED_NAME`
- `IMPORT_PATH_NOT_FOUND`, `IMPORT_PATH_INVALID`, `IMPORT_PATH_NO_ROOT_PROTOTYPE`
- `TYPE_MISMATCH_ASSIGNMENT`
- `STATIC_EMPTY_POP`, `MISSING_TYPE_CONTEXT`, `RETURN_SELF`
- `GROUP_NON_SCALAR_TYPE`, `GROUP_TYPE_MISMATCH`, `GROUP_MUTATION`
- `CONST_REASSIGNMENT`, `SEALED_INHERITANCE`
- `INVALID_PROTO_FIELD_INITIALIZER`, `PROTO_CONST_INIT_REQUIRED`
- `INVALID_VISIBILITY_LOCATION`, `INTERNAL_VISIBILITY_VIOLATION`

### 5.3 Runtime Error Codes

From `c/runtime/ps_errors.c`, `src/runtime.js`, tests:

- `R1001` `RUNTIME_INT_OVERFLOW`
- `R1002` `RUNTIME_INDEX_OOB`
- `R1003` `RUNTIME_MISSING_KEY`
- `R1004` `RUNTIME_DIVIDE_BY_ZERO`
- `R1005` `RUNTIME_SHIFT_RANGE`
- `R1006` `RUNTIME_EMPTY_POP`
- `R1007` `RUNTIME_INVALID_UTF8`
- `R1008` `RUNTIME_BYTE_RANGE`
- `R1010` `RUNTIME_TYPE_ERROR` / `RUNTIME_IO_ERROR` / `RUNTIME_JSON_ERROR` / `RUNTIME_MODULE_ERROR`
- `R1011` `UNHANDLED_EXCEPTION`
- `R1012` `RUNTIME_VIEW_INVALID`

## 6. Global Symbols

### 6.1 Global Namespace Model

- Single implicit global namespace for declarations in a compilation unit.
- User-defined global symbols:
- functions
- prototypes
- groups
- imported symbols/aliases

### 6.2 Entry Point

- `main` function is runtime entrypoint (`ps_vm_run_main` path).
- Accepted signatures are enforced by frontend/runtime pipeline (see manual/tests).

### 6.3 Auto-injected/Built-in Prototypes

Auto-available by implementation layer (exact set differs between C frontend and Node runtime):

- Common core: `Exception`, `RuntimeException`, `CivilDateTime`, `PathInfo`, `PathEntry`, `Dir`, `Walker`, `RegExpMatch`.
- Runtime Node adds: `RegExp`, `JSONValue`, `ProcessEvent`, `ProcessResult` and extra process/env exceptions.

### 6.4 Runtime Invisible Built-ins (IR-level)

- `call_builtin_print`
- `call_builtin_tostring`
- `pop_handler`

These are internal IR/VM operations, not user-callable language symbols.

## 7. Operators and Tokens

### 7.1 Token Classes

From `src/frontend.js` lexer:

- `kw` (keywords)
- `id` (identifier)
- `num` (numeric literal)
- `str` (string literal)
- `sym` (symbol/operator)
- `eof`

### 7.2 Numeric Literal Forms

- Decimal integer
- Decimal float (with optional exponent)
- Hex integer: `0x...`
- Binary integer: `0b...`
- Leading-dot float: `.123`

### 7.3 String Escapes

- `\"`, `\\`, `\n`, `\t`, `\r`, `\b`, `\f`, `\uXXXX`

### 7.4 Comments

- Line comment: `// ...`
- Block comment: `/* ... */`

### 7.5 Multi-char Symbols

- `...`
- `== != <= >= && || << >> ++ -- += -= *= /=`

### 7.6 Single-char Symbols

- `{ } ( ) [ ] ; , : . ?`
- `+ - * / %`
- `& | ^ ~ ! = < >`

### 7.7 Grammar-Level Constructions

Parser recognizes:

- imports (`import`, alias, selective import, import by path literal)
- prototype declarations (`prototype`, optional `sealed`, inheritance)
- group declarations (`T group Name { ... }`)
- function declarations (including variadic)
- statements:
- block
- variable declarations (`var`, typed decl, `const`)
- `if` / `else`
- `while`, `do...while`, `for` (`in`/`of` iterator forms and classic form)
- `switch` / `case` / `default`
- `try` / `catch` / `finally`
- `throw`, `return`, `break`, `continue`
- expressions:
- assignment and compound assignment
- ternary `?:`
- logical/binary/unary ops
- cast expression `(type)expr`
- member access, call, indexing, postfix/prefix inc/dec
- list/map literals

## 8. Lexical Summary Table

| Area | Inventory (current repo state) |
|---|---|
| Reserved keywords | 41 (`src/frontend.js` KEYWORDS) |
| Fundamental scalar types | `bool, byte, int, float, glyph, string, void` |
| Generic type constructors | `list, map, slice, view` |
| Built-in named types | `Exception, RuntimeException, CivilDateTime, TextFile, BinaryFile, PathInfo, PathEntry, Dir, Walker, JSONValue, RegExp, RegExpMatch, ProcessEvent, ProcessResult` |
| Standard native modules | `Math, Debug, Io, Fs, Sys, JSON, Time, TimeCivil, RegExp` |
| Test-only modules in registry | `test.simple, test.utf8, test.env, test.throw, test.noinit, test.badver, test.nosym, test.missing, test.invalid` |
| Static error codes | `E0001,E0002,E0003,E1001,E1002,E1003,E2001,E2002,E2003,E2004,E3001,E3003,E3004,E3005,E3006,E3007,E3120,E3121,E3122,E3130,E3131,E3140,E3150,E3151,E3200,E3201` |
| Runtime error codes | `R1001,R1002,R1003,R1004,R1005,R1006,R1007,R1008,R1010,R1011,R1012` |
| Internal runtime-only ops | `call_builtin_print, call_builtin_tostring, pop_handler` |

## Verification Notes

### Files analyzed

- `SPECIFICATION.md`
- `MANUEL_REFERENCE.md`
- `src/frontend.js`
- `src/runtime.js`
- `src/ir.js`
- `src/c_backend.js`
- `c/frontend.c`
- `c/runtime/ps_vm.c`
- `c/runtime/ps_errors.c`
- `c/modules/*.c`
- `tests/modules_src/*.c`
- `modules/registry.json`
- `tests/**/*.expect.json`
- `tests/**/*.pts`

### Method used

- Lexer/token inventory from explicit token sets and parser entrypoints.
- Type/method inventory from frontend arity+return-type dispatch tables.
- Module and global constants inventory from `modules/registry.json`.
- Error codes/categories from frontend/runtime code and test expectations.
- Cross-check of runtime internal built-ins from IR and VM op handlers.

### Explicit limits / ambiguities in current codebase

- There is no single canonical, centralized source of truth for all built-ins across all runtimes; inventories are split between frontend C, frontend JS, runtime JS, registry, and C runtime.
- `src/runtime.js` declares some runtime-only prototypes/exceptions not mirrored in `c/frontend.c` auto-injected prototype list.
- `modules/registry.json` includes both standard modules and test harness modules; both are listed above and distinguished.

If a stricter normative boundary is required (language core only vs. test/runtime-only symbols), split this cartography into two documents (normative core / implementation extensions).
