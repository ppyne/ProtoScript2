# ProtoScript2 — Module Sys — Process Execution Specification

Status: Normative\
Scope: POSIX systems only (Linux, BSD, macOS) Module: Standard Native Module `Sys`

---

# 1. Overview

This specification defines the process execution API provided by the standard native module `Sys`.

The design strictly follows ProtoScript2 principles:

- synchronous execution only
- no implicit shell
- no implicit text decoding
- explicit binary I/O
- deterministic resource handling
- no asynchronous callbacks
- no background execution
- no signal handling API

The API provides controlled execution of a child process with explicit argument passing and optional capture of stdout/stderr.

---

# 2. Function Signature

```
Sys.execute(
    string program,
    list<string> args,
    list<byte> input,
    bool captureStdout,
    bool captureStderr
) : ProcessResult
```

This function executes a program synchronously and returns only after the child process has terminated.

---

# 3. Semantics

## 3.1 Execution Model

- The function MUST use `fork()` and `execve()` (or equivalent POSIX mechanism).
- The program is executed directly. No shell invocation is permitted.
- Argument parsing is not performed.
- `program` is executed exactly as provided.
- `args` are passed verbatim to `execve()`.

No implicit PATH search is performed unless explicitly documented by the runtime.

---

## 3.2 Standard Input

- `input` is written to the child process standard input.
- If `input.length == 0`, stdin is immediately closed (EOF).
- All bytes MUST be written before waiting for process termination.
- After writing, stdin MUST be closed.

No streaming interaction is supported.

---

## 3.3 Output Capture

If `captureStdout` is true:

- stdout is captured.

If `captureStderr` is true:

- stderr is captured.

If capture flags are false:

- the corresponding stream inherits the parent process descriptor.

Captured output MUST be recorded chronologically as described in section 5.

---

# 4. ProcessResult Prototype

```
prototype ProcessResult {
    function exitCode() : int {}
    function events() : list<ProcessEvent> {}
}
```

## 4.1 exitCode

- The exit code returned by the process.
- If terminated by signal, implementation-defined mapping is permitted but must be documented.

## 4.2 events

A chronologically ordered list of output events.

- Empty if neither stdout nor stderr is captured.

---

# 5. ProcessEvent Prototype

```
prototype ProcessEvent {
    function stream() : int {}
    function data() : list<byte> {}
}
```

## 5.1 stream

- `1` represents stdout
- `2` represents stderr

No other values are permitted.

## 5.2 data

- A contiguous block of bytes read from the corresponding stream.
- May be empty only if OS returns zero-length read before closure (rare).
- No decoding is performed.

---

# 6. Ordering Guarantees

The `events` list MUST preserve the chronological order in which data was read from the child process file descriptors.

Implementation requirements:

- The parent MUST multiplex stdout and stderr using `poll()`, `select()`, or equivalent.
- Events MUST be appended in the order they are observed.
- No reordering is permitted.

The exact chunk size is implementation-defined.

---

# 7. Error Model

The module `Sys` MUST define the following additional sealed exception prototypes:

- `ProcessCreationException : RuntimeException`
- `ProcessExecutionException : RuntimeException`
- `ProcessPermissionException : RuntimeException`
- `InvalidExecutableException : RuntimeException`
- `InvalidArgumentException : RuntimeException`
- `IOException : RuntimeException`

Errors MUST be raised in the following cases:

- invalid `program` path → `InvalidExecutableException`
- permission denied → `ProcessPermissionException`
- fork failure → `ProcessCreationException`
- exec failure → `ProcessExecutionException`
- invalid arguments → `InvalidArgumentException`
- unexpected system error → `IOException`

---

# 8. Determinism and Resource Handling

- The function is synchronous.
- The parent process MUST wait for child termination using `waitpid()`.
- All file descriptors MUST be closed deterministically.
- No file descriptor leaks are permitted.

Memory usage is proportional to the total captured output.

---

# 9. Non-Goals

The following are explicitly not supported:

- asynchronous execution
- background processes
- streaming callbacks
- incremental consumption
- shell invocation
- signal handling
- environment modification

---

# 10. Example

```ps
import Sys;

function main() : void {
    ProcessResult r = Sys.execute(
        "/bin/echo",
        ["hello"],
        [],
        true,
        true
    );

    for (ProcessEvent e in r.events()) {
        // explicit decoding if needed
    }
}
```

---

# End of Specification
