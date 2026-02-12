# ProtoScript2 — Module Sys Specification

Status: Normative  
Scope: POSIX systems only (Linux, BSD, macOS)

---

# 1. Overview

The `Sys` module provides minimal, read-only access to selected process-level information.

Design goals:

- no global mutable state exposure
- no environment mutation
- no signal handling API
- no process control primitives
- deterministic, explicit behavior
- strict error typing

The module is intentionally minimal.

---

# 2. Module Resolution

```ps
import Sys;
```

The module is resolved statically through the ProtoScript2 module registry.

A missing module is a compile-time error.

The native implementation must be provided by the host runtime.

---

# 3. Error Model

## 3.1 Exception Prototypes

The `Sys` module defines the following sealed exception prototypes:

- `InvalidArgumentException : RuntimeException`
- `EnvironmentAccessException : RuntimeException`
- `InvalidEnvironmentNameException : RuntimeException`
- `IOException : RuntimeException`

All runtime failures defined in this specification MUST throw one of the above.

## 3.2 Native ABI Contract

Native code MUST signal failure using:

- `ps_throw(ctx, <PS_ERR_*>, <message>)`
- return `PS_ERR`

The runtime constructs the corresponding exception instance.

---

# 4. Environment Access (Read-Only)

The `Sys` module exposes read-only access to environment variables.

Environment mutation (setenv, unsetenv, putenv) is NOT supported.

The environment is treated as immutable from the perspective of ProtoScript2 code.

---

# 5. Sys.hasEnv

## Sys.hasEnv(name: string) : bool

Returns `true` if an environment variable named `name` exists.

### Parameter Rules

- `name` MUST be a `string`.
- `name` MUST NOT be empty.
- `name` MUST NOT contain the character `=`.

If any rule is violated, `InvalidEnvironmentNameException` MUST be thrown.

### Return Semantics

- Returns `true` if the variable exists in the process environment.
- Returns `false` otherwise.

### Error Conditions

Throws:

- `InvalidEnvironmentNameException`
- `EnvironmentAccessException` (unexpected system failure)
- `IOException` (low-level system failure)

---

# 6. Sys.env

## Sys.env(name: string) : string

Returns the value of the environment variable named `name`.

### Parameter Rules

- `name` MUST be a `string`.
- `name` MUST NOT be empty.
- `name` MUST NOT contain the character `=`.

Violation of these rules MUST throw `InvalidEnvironmentNameException`.

### Return Semantics

- Returns the environment variable value as a `string`.
- If the variable does not exist, MUST throw `EnvironmentAccessException`.

No sentinel values are used.

### Encoding

Returned values are treated as UTF-8 byte sequences.

- If the underlying environment value cannot be represented as valid UTF-8, `EnvironmentAccessException` MUST be thrown.

### Error Conditions

Throws:

- `InvalidEnvironmentNameException`
- `EnvironmentAccessException`
- `IOException`

---

# 7. Determinism and Semantics

The environment is process-scoped and externally mutable by the operating system.

ProtoScript2 does not attempt to provide determinism over environment content.

The `Sys` module guarantees:

- no caching
- no mutation
- no implicit normalization
- no fallback values

Each call reflects the current process environment at call time.

---

# 8. Non-Goals

The following are explicitly not supported:

- setting environment variables
- deleting environment variables
- enumerating all environment variables
- signal handling
- process termination (`exit`)

These concerns are outside the scope of ProtoScript2’s core runtime model.

---

# 9. Implementation Constraints

- The implementation MUST rely on standard POSIX `getenv` semantics.
- No memory owned by the OS environment may be exposed directly; returned strings MUST be runtime-managed copies.
- The implementation MUST NOT modify the process environment.

---

# End of Specification

