# Diagnostic Cases (normative)

## Parse unexpected token
- Context: parser
- Spec §: 10.2
- Message template: `<file>:<line>:<column> E1001 PARSE_UNEXPECTED_TOKEN: unexpected token '<tok>', expecting 'expression'`
- Code: E1001
- Example input:
```c
function add(int a, int b) : int {
    return a + , b;
}
```
- Expected output exact:
```
invalid/parse/unexpected_token_comma.pts:2:16 E1001 PARSE_UNEXPECTED_TOKEN: unexpected token ',', expecting 'expression'
```

## Type mismatch assignment
- Context: static typing
- Spec §: 10.2
- Message template: `<file>:<line>:<column> E3001 TYPE_MISMATCH_ASSIGNMENT: cannot assign <rhs> to <lhs>`
- Code: E3001
- Example input:
```c
function main() : void {
    int x = 1;
    x = "hello";
}
```
- Expected output exact:
```
invalid/type/type_mismatch_assignment.pts:3:5 E3001 TYPE_MISMATCH_ASSIGNMENT: cannot assign string to int
```

## Redeclaration in same scope
- Context: static name binding / lexical scope
- Spec §: 15.2
- Message template: `<file>:<line>:<column> E3131 REDECLARATION: redeclaration of local '<name>' in the same scope`
- Code: E3131
- Example input:
```c
import Io;

function main(): void {
    string t = Io.tempPath();
    TextFile t = Io.openText(t, "w");
}
```
- Expected output exact:
```
invalid/type/redeclaration_same_scope.pts:5:14 E3131 REDECLARATION: redeclaration of local 't' in the same scope
```

## Import path not found
- Context: module resolution (static)
- Spec §: 10.2
- Message template: `<file>:<line>:<column> E2002 IMPORT_PATH_NOT_FOUND: import path not found`
- Code: E2002
- Example input:
```c
import "./missing.pts";
```
- Expected output exact:
```
invalid/type/module_import_path_missing.pts:1:1 E2002 IMPORT_PATH_NOT_FOUND: import path not found
```

## Unhandled exception
- Context: runtime / exception handling
- Spec §: 10.5
- Message template: `<file>:<line>:<column> R1011 UNHANDLED_EXCEPTION: unhandled exception. got <ExceptionType>("message"); expected matching catch`
- Code: R1011
- Example input:
```c
function main() : void {
    Exception e = Exception.clone();
    e.message = "boom";
    throw e;
}
```
- Expected output exact:
```
invalid/runtime/unhandled_exception.pts:4:5 R1011 UNHANDLED_EXCEPTION: unhandled exception. got Exception("boom"); expected matching catch
```

## Division by zero
- Context: runtime arithmetic
- Spec §: 10.2, 11.2
- Message template: `<file>:<line>:<column> R1004 RUNTIME_DIVIDE_BY_ZERO: division by zero. got 0; expected non-zero divisor`
- Code: R1004
- Example input:
```c
function main() : void {
    int a = 1;
    int b = 0;
    int c = a / b;
}
```
- Expected output exact:
```
invalid/runtime/div_zero_int.pts:6:17 R1004 RUNTIME_DIVIDE_BY_ZERO: division by zero. got 0; expected non-zero divisor
```

## Missing key in map
- Context: runtime map access
- Spec §: 11.4
- Message template: `<file>:<line>:<column> R1003 RUNTIME_MISSING_KEY: missing key. got <key>; expected present key`
- Code: R1003
- Example input:
```c
import Io;

function main() : void {
    map<string, int> m = {"a": 1};
    int x = m["missing"];
    Io.printLine(x.toString());
}
```
- Expected output exact:
```
invalid/runtime/map_missing_key.pts:5:13 R1003 RUNTIME_MISSING_KEY: missing key. got "missing"; expected present key
```

## Preprocessor line mapping
- Context: preprocessor + runtime
- Spec §: 10.2, preprocessing mapping rule
- Message template: `<file>:<line>:<column> R1004 RUNTIME_DIVIDE_BY_ZERO: division by zero. got 0; expected non-zero divisor`
- Code: R1004
- Example input:
```c
#line 200 "mapped_file.pts"
function main() : void {
    int x = 0;
    int y = 1 / x;
}
```
- Expected output exact:
```
mapped_file.pts:202:17 R1004 RUNTIME_DIVIDE_BY_ZERO: division by zero. got 0; expected non-zero divisor
```

## Catch type must derive from Exception
- Context: static typing (exceptions)
- Spec §: 10.5
- Message template: `<file>:<line>:<column> E3001 TYPE_MISMATCH_ASSIGNMENT: catch type must derive from Exception`
- Code: E3001
- Example input:
```c
function main() : void {
    try {
        int x = 1;
    } catch (int e) {
        int y = 2;
    }
}
```
- Expected output exact:
```
invalid/type/catch_non_exception.pts:4:18 E3001 TYPE_MISMATCH_ASSIGNMENT: catch type must derive from Exception
```
