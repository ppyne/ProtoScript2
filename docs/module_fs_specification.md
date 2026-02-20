# ProtoScript2 — Module Fs Specification

Status: Normative  
Scope: POSIX systems only (Linux, BSD, macOS)

---

# 1. Overview

The `Fs` module provides synchronous filesystem primitives for ProtoScript2.

The design strictly follows ProtoScript2 principles:

- explicit control over side effects
- no sentinel values
- no implicit absence values
- no mixed error models
- deterministic behavior
- no hidden normalization
- no implicit recursion
- no bulk implicit allocation

The `Fs` module is a native module loaded through the ProtoScript2 module system.

Availability is determined by the module registry used by the compiler and runtime.

- Imports are resolved statically against a JSON registry (no wildcard imports).
- The registry location may be overridden according to the module search rules.

A missing module or missing symbol is a compile-time error.

At runtime / link time:

- the host runtime must provide the required native module implementation; otherwise the build must fail at link/load time.
- no implicit fallback is permitted.

---

# 2. Error Model and ABI Mapping

## 2.1 Exception objects (language semantics)

ProtoScript2 represents runtime errors as exception objects.

- Only instances of prototypes deriving from `Exception` may be thrown.
- Runtime violations raised by the runtime (or native modules) must throw an instance deriving from `RuntimeException`.
- `RuntimeException` instances expose, at minimum: `file`, `line`, `column`, `message`, `cause`, `code`, and `category`.

`catch (T e)` matches by exception prototype type (including parent/child substitution).

## 2.2 Fs exception prototypes (module contract)

`Fs` defines the following sealed exception prototypes:

- `FileNotFoundException : RuntimeException`
- `NotADirectoryException : RuntimeException`
- `NotAFileException : RuntimeException`
- `PermissionDeniedException : RuntimeException`
- `DirectoryNotEmptyException : RuntimeException`
- `InvalidPathException : RuntimeException`
- `IOException : RuntimeException`

These prototypes allow selective catching without string inspection.

## 2.3 Native ABI requirements

Native code must raise failures via the public C ABI error mechanism:

- call `ps_throw(ctx, <PS_ERR_*>, <message>)`
- return `PS_ERR`

The runtime maps native error codes to a `RuntimeException` instance and selects the appropriate `Fs` exception prototype.

## 2.4 Atomicity under exceptions

Filesystem state must never be partially modified if an exception is thrown.

---

# 3. Core Queries (Never Throw on Logical False)

These functions answer state questions and never throw for normal negative results.

## Fs.exists(string path) : bool

Returns `true` if the path exists.

## Fs.isFile(string path) : bool

Returns `true` if path is a regular file.

## Fs.isDir(string path) : bool

Returns `true` if path is a directory.

## Fs.isSymlink(string path) : bool

Returns `true` if path is a symbolic link.

## Fs.isReadable(string path) : bool

Returns `true` if the file or directory is readable by the effective user of the current process.

## Fs.isWritable(string path) : bool

Returns `true` if the file or directory is writable by the effective user of the current process.

## Fs.isExecutable(string path) : bool

Returns `true` if the file is executable by the effective user of the current process.

Broken symlink rules:

- `exists` returns `true`.
- `isFile` and `isDir` return `false`.

Logical negative answers must return `false`.

Exceptional OS failures must throw `IOException` or `InvalidPathException`.

---

# 4. Value-Returning Operations (Throw on Failure)

## Fs.size(string path) : int

Returns file size in bytes.

Throws:

- `FileNotFoundException`
- `NotAFileException`
- `PermissionDeniedException`
- `IOException`

No sentinel values are used.

---

# 5. Mutating Operations (Throw on Failure)

All mutating operations throw on failure.

## Fs.mkdir(string path) : void

## Fs.rmdir(string path) : void

## Fs.rm(string path) : void

## Fs.cp(string source, string destination) : void

## Fs.mv(string source, string destination) : void

## Fs.chmod(string path, int mode) : void

These operations throw one of:

- `FileNotFoundException`
- `NotADirectoryException`
- `NotAFileException`
- `DirectoryNotEmptyException`
- `PermissionDeniedException`
- `InvalidPathException`
- `IOException`

Atomicity guarantee:

If an exception is thrown:

- destination remains unchanged
- no partial file writes are observable

---

# 6. Working Directory

## Fs.cwd() : string

Returns current working directory.

Throws `IOException` on OS failure.

## Fs.cd(string path) : void

Throws:

- `FileNotFoundException`
- `NotADirectoryException`
- `PermissionDeniedException`
- `IOException`

---

# 7. Path Analysis

## Fs.pathInfo(string path) : PathInfo

Returns a `PathInfo` prototype instance.

Throws:

- `InvalidPathException`
- `IOException`

---

# Prototype PathInfo

`PathInfo` is a native prototype with fixed API.

Methods:

- `dirname() : string`
- `basename() : string`
- `filename() : string`
- `extension() : string`

No normalization is performed.

The field set is closed and cannot be extended.

---

## Example

```
string path = "/path/to/the/file.tar.gz";
PathInfo pi = Fs.pathInfo(path);

string a = pi.dirname();
string b = pi.basename();
string c = pi.filename();
string d = pi.extension();
```

---

# 8. Directory Iteration Model

`Fs.ls()` is intentionally not provided.

Directory traversal uses a strict streaming iterator.

---

# 9. Fs.openDir

```
Fs.openDir(string path) : Dir
```

Throws:

- `FileNotFoundException`
- `NotADirectoryException`
- `PermissionDeniedException`
- `IOException`

---

# 10. Prototype Dir

Methods:

```
Dir.hasNext() : bool
Dir.next() : string
Dir.close() : void
Dir.reset() : void
```

## Dir.hasNext() : bool

- Returns `true` if a subsequent call to `next()` will produce an entry.
- Returns `false` when iteration is complete.
- Must not advance the iterator.

## Dir.next() : string

- Returns the next entry name.
- Must only be called when `hasNext()` returns `true`.
- If called when `hasNext()` returns `false`, throws `IOException`.
- Never returns `.` or `..`.

The implementation must filter out `.` and `..` before exposing entries to the language level.

## Dir.close() : void

Releases the native directory handle.

## Dir.reset() : void

Rewinds the directory stream.

---

## Simple Directory Listing Example

```
function list(string path) : void {
    Dir d = Fs.openDir(path);

    while (d.hasNext()) {
        string name = d.next();
        Io.printLine(name);
    }

    d.close();
}

list(".");
```

---

# 11. Recursive Traversal Example

```
function walk(string path) : void {
    Dir d = Fs.openDir(path);

    while (d.hasNext()) {
        string name = d.next();
        string full = path + "/" + name;
        if (Fs.isDir(full)) {
            walk(full);
        } else {
            Io.printLine(full);
        }
    }

    d.close();
}
```

---

# 12. Optional Walker Prototype

```
Fs.walk(string path, int maxDepth, bool followSymlinks) : Walker
```

Prototype Walker:

```
Walker.hasNext() : bool
Walker.next() : PathEntry
Walker.close() : void
```

Prototype PathEntry methods:

- `path() : string`
- `name() : string`
- `depth() : int`
- `isDir() : bool`
- `isFile() : bool`
- `isSymlink() : bool`

Traversal must be implemented iteratively.

---

# 13. Permissions

Capability queries return `false` when access is denied.

Operational functions throw `PermissionDeniedException`.

---

# 14. POSIX Scope

The module relies strictly on:

- `opendir`, `readdir`, `closedir`
- `stat`, `lstat`
- `chmod`
- `rename`
- `unlink`

No Windows compatibility layer.

---

# 15. Implementation Constraints

Mandatory constraints:

- no full-directory buffering
- no implicit recursion
- no hidden large allocations
- deterministic handle lifecycle
- explicit resource release

The module must remain mechanically predictable.

---

# End of Specification
