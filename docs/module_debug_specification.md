# ProtoScript2 — Module Debug — Specification

Status: Normative  
Module type: Standard native module (C implementation, mirrored in Node runtime)  

---

## 1. Overview

The `Debug` module provides diagnostic inspection facilities for ProtoScript2 values.

Design constraints:

- No reflective API is exposed to user code (no structure returned, only textual output).
- Deterministic output: same value → same textual representation (given same limits).
- Fully resolves prototype delegation chains.
- Displays origin of fields and methods.
- No `null` or `none` handling (language does not define such value).

The module is intended strictly for development and debugging.

---

## 2. Import

```ps
import Debug;
```

---

## 3. Public API

### 3.1 `Debug.dump`

```ps
Debug.dump(value: any) : void
```

Behavior:

- Writes a structured textual representation of `value` to **stderr**.
- Produces UTF-8 output.
- Has no side effects on inspected values.
- Does not invoke user-defined methods.
- Must not throw, except on unrecoverable write failure.

---

## 4. Output Format Rules

The exact formatting may evolve, but the following structural guarantees are normative.

### 4.1 General Conventions

- Indentation: 2 spaces per nesting level.
- Strings printed with double quotes and escaped characters (`\"`, `\\`, `\n`, etc.).
- Stable ordering everywhere.

---

## 5. Scalar Types

### 5.1 bool

```
bool(true)
bool(false)
```

### 5.2 byte

```
byte(0)
byte(255)
```

### 5.3 int

```
int(42)
int(-7)
```

### 5.4 float

```
float(3.14)
float(NaN)
float(Infinity)
```

### 5.5 glyph

```
glyph(U+0041)
```

### 5.6 string

```
string(len=5) "Hello"
```

Length is expressed in glyph count.

---

## 6. Collections

### 6.1 list<T>

Format:

```
list<int>(len=3) [
  [0] int(1)
  [1] int(2)
  [2] int(3)
]
```

- Elements are shown in index order.
- Type parameter `T` must be displayed.

### 6.2 map<K,V>

Format:

```
map<string,int>(len=2) {
  ["a"] int(1)
  ["b"] int(2)
}
```

- Entries are shown in insertion order.
- Key and value types are displayed.

### 6.3 slice<T> and view<T>

```
slice<int>(len=4) [ ... ]
view<byte>(len=8) [ ... ]
```

Must indicate that the structure is a view.

---

## 7. Prototype Instances

For an object created from a prototype:

The dump MUST display:

1. Dynamic prototype name
2. Delegation chain
3. Visible fields with:
   - Type
   - Current value
   - Declaring prototype
4. Visible methods with:
   - Name
   - Parameter list (names + types)
   - Return type
   - Declaring prototype
   - Override indication if applicable

### 7.1 Delegation Chain

Displayed from most derived to root:

```
delegation: ColoredPoint -> Point
```

If a prototype in the chain is declared `sealed`, it MUST be prefixed with the keyword:

```
delegation: Child -> sealed Parent
```

### 7.2 Fields

Fields are displayed grouped by declaring prototype.

Order rule (normative):

- Prototypes listed from most derived to root.
- Within each prototype: fields in source declaration order.

Example:

```
fields:
  [ColoredPoint] color : int = int(3)
  [Point] x : int = int(10)
  [Point] y : int = int(20)
```

Field declarations have no modifiers; `const` is not valid on fields and must never appear in debug output.

### 7.3 Methods

Displayed grouped by declaring prototype.

Signature format:

```
[ColoredPoint] move(dx:int, dy:int) : void
```

If overriding occurs:

```
[ColoredPoint] move(dx:int, dy:int) : void  (overrides Point.move)
```

Order rule:

- Prototypes from most derived to root.
- Within each prototype: methods in declaration order.

---

## 8. Cycle Detection

The implementation MUST prevent infinite recursion.

When a previously visited object is encountered:

```
@cycle#1
```

Identifiers are local to a single dump call.

---

## 9. Limits

Default limits (implementation defined but recommended):

- Max depth: 6
- Max elements per collection: 100
- Max string length displayed: 200 glyphs

When truncated:

```
... (truncated)
```

---

## 10. Error Handling

- Unknown internal type: display `unknown(<tag>)`.
- Write failure: may raise runtime write exception.

---

## 11. Determinism Guarantee

`Debug.dump` MUST be deterministic with respect to:

- Delegation resolution
- Field ordering
- Method ordering
- Map iteration

No memory addresses or runtime-specific identifiers may appear.

---

## 12. Groups (T group)

Group declarations produce a nominal descriptor value that may be passed to `Debug.dump` (e.g., `Debug.dump(Color)` where `int group Color { ... }`).
When a group descriptor is dumped, the implementation MUST display the group definition in a structured and deterministic form.

A group is a named symbolic enumeration over a scalar base type.

When dumping a group descriptor (not a value of the base scalar type), the output MUST include:

- The base type
- The group name
- All symbolic members
- The associated constant values
- Declaration order preserved
- No modifiers (group declarations do not accept modifiers)

Example:

```
group int Color {
  Black   = int(0x000000)
  Blue    = int(0x0000FF)
  Green   = int(0x00FF00)
  Cyan    = int(0x00FFFF)
  Red     = int(0xFF0000)
  Magenta = int(0xFF00FF)
  Yellow  = int(0xFFFF00)
  White   = int(0xFFFFFF)
}
```

If a value of the group type is dumped (for example `Color.Blue`), the implementation SHOULD display both:

- The symbolic name (if resolvable)
- The underlying scalar value

Example:

```
Color.Blue = int(0x0000FF)
```

If no symbolic name matches the scalar value, only the scalar form is printed.

The ordering of group members MUST follow source declaration order.

---

## 13. Non-Goals

- No JSON output mode
- No serialization
- No runtime reflection API
- No modification of inspected values

---

End of specification.
