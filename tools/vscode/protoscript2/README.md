# ProtoScript2 VS Code Extension

This extension provides editor support for the ProtoScript2 language, including a lightweight Language Server for smarter completions.

## Features

- Syntax highlighting (TextMate grammar)
- Completions for keywords, types, and preprocessor directives
- Member completions after `obj.` for built-in types and documented modules
- Member completions for `prototype` fields and methods
- Signature help (argument hints) for functions and methods
- Snippets for common language constructs

## Limitations

- No compiler integration
- The LSP uses lightweight parsing and documentation scanning (no compiler required)

## Local Installation

Prerequisites:

- Node.js (includes `npm`)
- VS Code CLI (`code` in your PATH)

VS Code does not install extensions directly from a folder path. The `code --install-extension` command expects either a published extension ID or a `.vsix` package.

### Package and install a `.vsix`

```bash
cd tools/vscode/protoscript2
npm install
npm run build
npm install -g @vscode/vsce
vsce package
code --install-extension protoscript2-0.1.0.vsix
```

## File Extensions

- `.pts`
