import * as path from "path";
import * as vscode from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;

const KEYWORDS = [
  "prototype",
  "function",
  "return",
  "if",
  "else",
  "while",
  "import"
];

const TYPES = [
  "int",
  "float",
  "bool",
  "byte",
  "string",
  "glyph",
  "list",
  "map"
];

const PREPROCESSOR = [
  "#include",
  "#define",
  "#ifdef",
  "#ifndef",
  "#endif"
];

export function activate(context: vscode.ExtensionContext) {
  const serverModule = context.asAbsolutePath(
    path.join("dist", "server.js")
  );

  const serverOptions: ServerOptions = {
    run: { module: serverModule, transport: TransportKind.ipc },
    debug: { module: serverModule, transport: TransportKind.ipc }
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ language: "protoscript2" }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher("**/*.pts")
    }
  };

  client = new LanguageClient(
    "protoscript2Lsp",
    "ProtoScript2 Language Server",
    serverOptions,
    clientOptions
  );

  const triggers = ["#", ..."abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"];
  const provider = vscode.languages.registerCompletionItemProvider(
    "protoscript2",
    {
      provideCompletionItems() {
        const items: vscode.CompletionItem[] = [];

        for (const keyword of KEYWORDS) {
          const item = new vscode.CompletionItem(
            keyword,
            vscode.CompletionItemKind.Keyword
          );
          items.push(item);
        }

        for (const type of TYPES) {
          const item = new vscode.CompletionItem(
            type,
            vscode.CompletionItemKind.TypeParameter
          );
          items.push(item);
        }

        for (const directive of PREPROCESSOR) {
          const item = new vscode.CompletionItem(
            directive,
            vscode.CompletionItemKind.Keyword
          );
          items.push(item);
        }

        return items;
      }
    },
    ...triggers
  );

  client.start();
  context.subscriptions.push(provider, client);
}

export async function deactivate() {
  if (!client) {
    return undefined;
  }
  return client.stop();
}
