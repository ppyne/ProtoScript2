# mcpp-wasm (Node/LSP)

This folder builds an autonomous WASM preprocessor for Node/LSP.
It does **not** depend on `web/` artifacts.

## Outputs

- `out/mcpp.wasm`
- `out/mcpp_node.js` (stable Node wrapper API)
- `out/mcpp_core.js` (Emscripten runtime glue)

## Build

From repository root:

```bash
make -C tools/mcpp-wasm
```

The build uses `third_party/mcpp` sources and compiles a dedicated bridge with Emscripten.
No system `mcpp` executable is required.

## API

```js
const mcpp = require("./out/mcpp_node.js");
const result = mcpp.preprocess(source, { file: "/path/file.pts" });
// result = { code, mapping: { preToOrigLine, origToPreLine } }
```

## VSCode packaging notes

The VSCode language server loads `src/frontend.js`, which loads this wrapper at:

- `tools/mcpp-wasm/out/mcpp_node.js`

Before packaging/testing the VSCode extension, build this artifact:

```bash
npm --prefix tools/vscode/protoscript2 run build:mcpp-wasm
```

Current test script already does this automatically.
