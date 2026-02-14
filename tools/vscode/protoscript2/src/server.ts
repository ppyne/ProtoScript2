import { fileURLToPath } from "url";
import {
  CompletionItem,
  CompletionItemKind,
  Diagnostic,
  DiagnosticSeverity,
  Hover,
  createConnection,
  InitializeResult,
  Location,
  ProposedFeatures,
  SignatureHelp,
  TextDocumentPositionParams,
  TextDocuments,
  TextDocumentSyncKind
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";

const frontend = require("./frontend.js");
const mcppModule = require("./mcpp/mcpp_node.js");
const connection = createConnection(ProposedFeatures.all);
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);

type FrontendDiag = {
  line?: number;
  column?: number;
  code?: string;
  name?: string;
  message: string;
};

type FrontendCompletion = {
  label: string;
  kind?: string;
  detail?: string;
};

type FrontendHover = {
  contents: string;
} | null;

type FrontendDefinition = {
  line: number;
  character: number;
} | null;

type FrontendSignature = {
  label: string;
  parameters: string[];
  activeParameter: number;
} | null;

type SemanticModel = {
  diagnostics: FrontendDiag[];
  queries: {
    completionsAt: (line: number, character: number) => FrontendCompletion[];
    hoverAt: (line: number, character: number) => FrontendHover;
    definitionAt: (line: number, character: number) => FrontendDefinition;
    signatureAt: (line: number, character: number) => FrontendSignature;
  };
};

const models = new Map<string, SemanticModel>();

function uriToFilePath(uri: string): string {
  try {
    return fileURLToPath(uri);
  } catch {
    return uri;
  }
}

function toDiagnostic(diag: FrontendDiag): Diagnostic {
  const line = Math.max(0, (diag.line ?? 1) - 1);
  const char = Math.max(0, (diag.column ?? 1) - 1);
  const code = diag.code || diag.name || "PS2";
  return {
    severity: DiagnosticSeverity.Error,
    range: {
      start: { line, character: char },
      end: { line, character: char + 1 }
    },
    message: diag.message,
    code,
    source: "protoscript2-frontend"
  };
}

function buildAndStoreModel(uri: string, text: string): SemanticModel | null {
  const filePath = uriToFilePath(uri);
  try {
    const model = frontend.buildSemanticModel(filePath, text, {
      usePreprocessor: true,
      mcppModule
    });
    models.set(uri, model);
    connection.sendDiagnostics({
      uri,
      diagnostics: (model.diagnostics || []).map(toDiagnostic)
    });
    return model;
  } catch (err) {
    const maybeDiag = (err as { diag?: FrontendDiag } | null)?.diag;
    if (maybeDiag && maybeDiag.message) {
      connection.sendDiagnostics({ uri, diagnostics: [toDiagnostic(maybeDiag)] });
    } else {
      const message = err instanceof Error ? err.message : String(err);
      const fallbackDiag: Diagnostic = {
        severity: DiagnosticSeverity.Error,
        range: {
          start: { line: 0, character: 0 },
          end: { line: 0, character: 1 }
        },
        message,
        source: "protoscript2-frontend"
      };
      connection.sendDiagnostics({ uri, diagnostics: [fallbackDiag] });
    }
    models.delete(uri);
    return null;
  }
}

function getModelForUri(uri: string): SemanticModel | null {
  const cached = models.get(uri);
  if (cached) {
    return cached;
  }
  const doc = documents.get(uri);
  if (!doc) {
    return null;
  }
  return buildAndStoreModel(uri, doc.getText());
}

connection.onInitialize((): InitializeResult => {
  return {
    capabilities: {
      textDocumentSync: TextDocumentSyncKind.Incremental,
      completionProvider: {
        triggerCharacters: ["."]
      },
      hoverProvider: true,
      definitionProvider: true,
      signatureHelpProvider: {
        triggerCharacters: ["(", ","]
      }
    }
  };
});

connection.onCompletion((params: TextDocumentPositionParams) => {
  const model = getModelForUri(params.textDocument.uri);
  if (!model) {
    return [];
  }
  const members = model.queries.completionsAt(params.position.line, params.position.character) || [];
  return members.map((member) => {
    const item: CompletionItem = {
      label: member.label,
      kind: member.kind === "method" ? CompletionItemKind.Method : CompletionItemKind.Field,
      detail: member.detail
    };
    return item;
  });
});

connection.onHover((params): Hover | null => {
  const model = getModelForUri(params.textDocument.uri);
  if (!model) {
    return null;
  }
  const hover = model.queries.hoverAt(params.position.line, params.position.character);
  if (!hover || !hover.contents) {
    return null;
  }
  return { contents: { kind: "markdown", value: hover.contents } };
});

connection.onDefinition((params): Location | null => {
  const model = getModelForUri(params.textDocument.uri);
  if (!model) {
    return null;
  }
  const def = model.queries.definitionAt(params.position.line, params.position.character);
  if (!def) {
    return null;
  }
  return {
    uri: params.textDocument.uri,
    range: {
      start: { line: def.line, character: def.character },
      end: { line: def.line, character: def.character + 1 }
    }
  };
});

connection.onSignatureHelp((params): SignatureHelp | null => {
  const model = getModelForUri(params.textDocument.uri);
  if (!model) {
    return null;
  }
  const sig = model.queries.signatureAt(params.position.line, params.position.character);
  if (!sig) {
    return null;
  }
  return {
    signatures: [
      {
        label: sig.label,
        parameters: sig.parameters.map((p) => ({ label: p }))
      }
    ],
    activeSignature: 0,
    activeParameter: Math.max(0, sig.activeParameter)
  };
});

documents.onDidOpen((event) => {
  buildAndStoreModel(event.document.uri, event.document.getText());
});

documents.onDidChangeContent((event) => {
  buildAndStoreModel(event.document.uri, event.document.getText());
});

documents.onDidClose((event) => {
  models.delete(event.document.uri);
  connection.sendDiagnostics({ uri: event.document.uri, diagnostics: [] });
});

documents.listen(connection);
connection.listen();
