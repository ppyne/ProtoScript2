# Robustness Sanitizer Policy

This project enforces an emit-c sanitizer warning policy via:

- `tests/robustness/run_asan_ubsan_emitc.sh`

## Compile Policy

- Reference compile flags:
  - `-std=c11 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g`
- Strict compile flags:
  - same flags + `-Werror -Wno-unused-function`

`-Wno-unused-function` is the only warning whitelist. It is used to neutralize monolithic runtime noise (`static` helpers not referenced by a given emitted test case).

## Layout Coverage Policy

The same script checks that every IR prototype parent/child pair is covered by generated `_Static_assert` rules in emitted C.

Allowed exclusions are explicit and limited to:

- `Exception:Object`
- `RuntimeException:Exception`

Normative reason:

- `Exception` and `RuntimeException` are handled through `ps_exception*` handles, not as regular layouted object structs in the prototype object layout chain. The generic parent/child struct layout assertions do not apply to these two links.
