# ProtoScript2 Divergence Report

## Dir

- Frontend C : present
- Frontend Node : missing
- Documentation : present
- Commentaire explicite : status=STDLIB; category=exception; locations=c/frontend.c:3907

## DirectoryNotEmptyException

- Frontend C : missing
- Frontend Node : present
- Documentation : missing
- Commentaire explicite : status=RUNTIME_ONLY; category=exception; locations=src/runtime.js:192

## EnvironmentAccessException

- Frontend C : missing
- Frontend Node : present
- Documentation : missing
- Commentaire explicite : status=RUNTIME_ONLY; category=exception; locations=src/runtime.js:180

## InvalidEnvironmentNameException

- Frontend C : missing
- Frontend Node : present
- Documentation : missing
- Commentaire explicite : status=RUNTIME_ONLY; category=exception; locations=src/runtime.js:181

## InvalidExecutableException

- Frontend C : missing
- Frontend Node : present
- Documentation : missing
- Commentaire explicite : status=RUNTIME_ONLY; category=exception; locations=src/runtime.js:179

## NotADirectoryException

- Frontend C : missing
- Frontend Node : present
- Documentation : missing
- Commentaire explicite : status=RUNTIME_ONLY; category=exception; locations=src/runtime.js:190

## NotAFileException

- Frontend C : missing
- Frontend Node : present
- Documentation : missing
- Commentaire explicite : status=RUNTIME_ONLY; category=exception; locations=src/runtime.js:191

## ProcessCreationException

- Frontend C : missing
- Frontend Node : present
- Documentation : missing
- Commentaire explicite : status=RUNTIME_ONLY; category=exception; locations=src/runtime.js:176

## ProcessEvent

- Frontend C : missing
- Frontend Node : present
- Documentation : missing
- Commentaire explicite : status=RUNTIME_ONLY; category=type; locations=src/runtime.js:90

## ProcessExecutionException

- Frontend C : missing
- Frontend Node : present
- Documentation : missing
- Commentaire explicite : status=RUNTIME_ONLY; category=exception; locations=src/runtime.js:177

## ProcessPermissionException

- Frontend C : missing
- Frontend Node : present
- Documentation : missing
- Commentaire explicite : status=RUNTIME_ONLY; category=exception; locations=src/runtime.js:178

## ProcessResult

- Frontend C : missing
- Frontend Node : present
- Documentation : missing
- Commentaire explicite : status=RUNTIME_ONLY; category=type; locations=src/runtime.js:100

## test.badver

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=module; locations=modules/registry.json:45, src/frontend.js:140, c/frontend.c:2920

## test.badver.ping

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=function; locations=modules/registry.json:47, src/frontend.js:140, c/frontend.c:3061

## test.env

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=module; locations=modules/registry.json:26, src/frontend.js:140, c/frontend.c:2920

## test.env.set

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=function; locations=modules/registry.json:28, src/frontend.js:140, c/frontend.c:3061

## test.env.setInvalidUtf8

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=function; locations=modules/registry.json:29, src/frontend.js:140, c/frontend.c:3061

## test.invalid

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=module; locations=modules/registry.json:63, src/frontend.js:140, c/frontend.c:2920

## test.invalid.bad

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=function; locations=modules/registry.json:65, src/frontend.js:140, c/frontend.c:3061

## test.missing

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=module; locations=modules/registry.json:57, src/frontend.js:140, c/frontend.c:2920

## test.missing.ping

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=function; locations=modules/registry.json:59, src/frontend.js:140, c/frontend.c:3061

## test.noinit

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=module; locations=modules/registry.json:39, src/frontend.js:140, c/frontend.c:2920

## test.noinit.ping

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=function; locations=modules/registry.json:41, src/frontend.js:140, c/frontend.c:3061

## test.nosym

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=module; locations=modules/registry.json:51, src/frontend.js:140, c/frontend.c:2920

## test.nosym.missing

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=function; locations=modules/registry.json:53, src/frontend.js:140, c/frontend.c:3061

## test.simple

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=module; locations=modules/registry.json:14, src/frontend.js:140, c/frontend.c:2920

## test.simple.add

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=function; locations=modules/registry.json:16, src/frontend.js:140, c/frontend.c:3061

## test.throw

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=module; locations=modules/registry.json:33, src/frontend.js:140, c/frontend.c:2920

## test.throw.fail

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=function; locations=modules/registry.json:35, src/frontend.js:140, c/frontend.c:3061

## test.utf8

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=module; locations=modules/registry.json:20, src/frontend.js:140, c/frontend.c:2920

## test.utf8.roundtrip

- Frontend C : present
- Frontend Node : present
- Documentation : present
- Commentaire explicite : status=TEST_ONLY; category=function; locations=modules/registry.json:22, src/frontend.js:140, c/frontend.c:3061

## Walker

- Frontend C : present
- Frontend Node : missing
- Documentation : present
- Commentaire explicite : status=STDLIB; category=exception; locations=c/frontend.c:3917

## Verification Notes

- Files analyzed: lexicon dataset + source files referenced by locations.
- Method: divergence computed from frontend presence flags and status classes.
- Ambiguities: documentation presence inferred from status and current docs scope.
- Limits: spec/code symbol matching is string-based.
