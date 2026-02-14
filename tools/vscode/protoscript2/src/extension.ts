import * as path from "path";
import * as vscode from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;

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

  client.start();
  context.subscriptions.push(client);
}

export async function deactivate() {
  if (!client) {
    return undefined;
  }
  return client.stop();
}
