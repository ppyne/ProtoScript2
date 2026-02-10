import * as fs from "fs";
import * as path from "path";
import { fileURLToPath } from "url";
import {
  CompletionItem,
  CompletionItemKind,
  createConnection,
  InitializeParams,
  InitializeResult,
  ProposedFeatures,
  SignatureHelp,
  SignatureInformation,
  TextDocumentPositionParams,
  TextDocuments,
  TextDocumentSyncKind
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";

type MemberInfo = {
  name: string;
  kind: CompletionItemKind;
  args: string[];
  detail: string;
};

type MemberMap = Map<string, MemberInfo>;

type SignatureMap = Map<string, MemberInfo>;

const connection = createConnection(ProposedFeatures.all);
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);

const BUILTIN_TYPES = new Set([
  "int",
  "float",
  "bool",
  "byte",
  "string",
  "glyph",
  "list",
  "map",
  "slice",
  "view",
  "void"
]);

const typeMembers: Map<string, MemberMap> = new Map();
const globalFunctions: SignatureMap = new Map();
let workspaceRoot: string | undefined;

function addMember(typeName: string, member: MemberInfo) {
  if (!typeMembers.has(typeName)) {
    typeMembers.set(typeName, new Map());
  }
  const members = typeMembers.get(typeName);
  if (!members) {
    return;
  }
  if (!members.has(member.name)) {
    members.set(member.name, member);
  }
}

function addFunction(member: MemberInfo) {
  if (!globalFunctions.has(member.name)) {
    globalFunctions.set(member.name, member);
  }
}

function normalizeType(typeName: string): string {
  const match = /([A-Za-z_][A-Za-z0-9_]*)/.exec(typeName);
  return match ? match[1] : typeName;
}

function parseArgs(raw: string): string[] {
  const trimmed = raw.trim();
  if (!trimmed) {
    return [];
  }
  return trimmed.split(",").map((part) => part.trim()).filter(Boolean);
}

function parseDocsText(text: string) {
  const methodRegex = /([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/g;
  let match: RegExpExecArray | null;
  while ((match = methodRegex.exec(text)) !== null) {
    const objectName = match[1];
    const methodName = match[2];
    const args = parseArgs(match[3]);
    addMember(objectName, {
      name: methodName,
      kind: CompletionItemKind.Method,
      args,
      detail: `${objectName}.${methodName}(${args.join(", ")})`
    });
  }

  const propertyRegex = /`([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)`/g;
  while ((match = propertyRegex.exec(text)) !== null) {
    const objectName = match[1];
    const propName = match[2];
    const existing = typeMembers.get(objectName);
    if (existing && existing.has(propName)) {
      continue;
    }
    addMember(objectName, {
      name: propName,
      kind: CompletionItemKind.Property,
      args: [],
      detail: `${objectName}.${propName}`
    });
  }

  const tableLineRegex = /^\s*\|.*\|/gm;
  let lineMatch: RegExpExecArray | null;
  while ((lineMatch = tableLineRegex.exec(text)) !== null) {
    const line = lineMatch[0];
    const dottedRegex = /([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)/g;
    let dottedMatch: RegExpExecArray | null;
    while ((dottedMatch = dottedRegex.exec(line)) !== null) {
      const objectName = dottedMatch[1];
      const memberName = dottedMatch[2];
      const existing = typeMembers.get(objectName);
      if (existing && existing.has(memberName)) {
        continue;
      }
      addMember(objectName, {
        name: memberName,
        kind: CompletionItemKind.Property,
        args: [],
        detail: `${objectName}.${memberName}`
      });
    }
  }
}

function parsePrototypeBlocks(text: string) {
  const protoRegex = /prototype\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{([\s\S]*?)\}/g;
  let match: RegExpExecArray | null;
  while ((match = protoRegex.exec(text)) !== null) {
    const protoName = match[1];
    const body = match[2];

    const fieldRegex = /^\s*([A-Za-z_][A-Za-z0-9_<>]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;/gm;
    let fieldMatch: RegExpExecArray | null;
    while ((fieldMatch = fieldRegex.exec(body)) !== null) {
      const fieldType = normalizeType(fieldMatch[1]);
      const fieldName = fieldMatch[2];
      addMember(protoName, {
        name: fieldName,
        kind: CompletionItemKind.Field,
        args: [],
        detail: `${fieldType} ${fieldName}`
      });
    }

    const methodRegex = /^\s*function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/gm;
    let methodMatch: RegExpExecArray | null;
    while ((methodMatch = methodRegex.exec(body)) !== null) {
      const methodName = methodMatch[1];
      const args = parseArgs(methodMatch[2]);
      addMember(protoName, {
        name: methodName,
        kind: CompletionItemKind.Method,
        args,
        detail: `${protoName}.${methodName}(${args.join(", ")})`
      });
    }
  }
}

function parseGlobalFunctions(text: string) {
  const funcRegex = /^\s*function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/gm;
  let match: RegExpExecArray | null;
  while ((match = funcRegex.exec(text)) !== null) {
    const name = match[1];
    const args = parseArgs(match[2]);
    addFunction({
      name,
      kind: CompletionItemKind.Function,
      args,
      detail: `${name}(${args.join(", ")})`
    });
  }
}

function collectVarTypes(text: string): Map<string, string> {
  const vars = new Map<string, string>();
  const knownTypes = new Set<string>([...BUILTIN_TYPES, ...typeMembers.keys()]);

  const declRegex = /\b([A-Za-z_][A-Za-z0-9_<>]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(=|;|,)/g;
  let match: RegExpExecArray | null;
  while ((match = declRegex.exec(text)) !== null) {
    const rawType = normalizeType(match[1]);
    const name = match[2];
    if (knownTypes.has(rawType)) {
      vars.set(name, rawType);
    }
  }

  const varStringRegex = /\bvar\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"/g;
  while ((match = varStringRegex.exec(text)) !== null) {
    vars.set(match[1], "string");
  }

  const varStringSingleRegex = /\bvar\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*'/g;
  while ((match = varStringSingleRegex.exec(text)) !== null) {
    vars.set(match[1], "string");
  }

  const varBoolRegex = /\bvar\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(true|false)\b/g;
  while ((match = varBoolRegex.exec(text)) !== null) {
    vars.set(match[1], "bool");
  }

  const varFloatRegex = /\bvar\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*\d+\.\d+/g;
  while ((match = varFloatRegex.exec(text)) !== null) {
    vars.set(match[1], "float");
  }

  const varIntRegex = /\bvar\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*\d+\b/g;
  while ((match = varIntRegex.exec(text)) !== null) {
    vars.set(match[1], "int");
  }

  const varCtorRegex = /\bvar\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_]*)\s*\./g;
  while ((match = varCtorRegex.exec(text)) !== null) {
    const name = match[1];
    const typeName = match[2];
    if (knownTypes.has(typeName)) {
      vars.set(name, typeName);
    }
  }

  return vars;
}

function indexWorkspaceDocs(root: string) {
  const docPaths: string[] = [];
  const docsDir = path.join(root, "docs");
  const manual = path.join(root, "MANUEL_REFERENCE.md");
  const spec = path.join(root, "SPECIFICATION.md");

  if (fs.existsSync(manual)) {
    docPaths.push(manual);
  }
  if (fs.existsSync(spec)) {
    docPaths.push(spec);
  }

  if (fs.existsSync(docsDir)) {
    const stack: string[] = [docsDir];
    while (stack.length) {
      const current = stack.pop();
      if (!current) {
        continue;
      }
      for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
        const fullPath = path.join(current, entry.name);
        if (entry.isDirectory()) {
          stack.push(fullPath);
        } else if (entry.isFile() && fullPath.endsWith(".md")) {
          docPaths.push(fullPath);
        }
      }
    }
  }

  for (const docPath of docPaths) {
    try {
      const text = fs.readFileSync(docPath, "utf8");
      parseDocsText(text);
      parsePrototypeBlocks(text);
      parseGlobalFunctions(text);
    } catch (err) {
      connection.console.warn(`Failed to read ${docPath}: ${String(err)}`);
    }
  }
}

function indexWorkspaceCode(root: string) {
  const stack: string[] = [root];
  const ignored = new Set(["node_modules", ".git", "tools", "third_party", "web"]);

  while (stack.length) {
    const current = stack.pop();
    if (!current) {
      continue;
    }
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      if (entry.isDirectory()) {
        if (ignored.has(entry.name)) {
          continue;
        }
        stack.push(path.join(current, entry.name));
      } else if (entry.isFile() && entry.name.endsWith(".pts")) {
        const filePath = path.join(current, entry.name);
        try {
          const text = fs.readFileSync(filePath, "utf8");
          parsePrototypeBlocks(text);
          parseGlobalFunctions(text);
        } catch (err) {
          connection.console.warn(`Failed to read ${filePath}: ${String(err)}`);
        }
      }
    }
  }
}

connection.onInitialize((params: InitializeParams): InitializeResult => {
  workspaceRoot = params.rootUri ? fileURLToPath(params.rootUri) : undefined;

  if (workspaceRoot) {
    indexWorkspaceDocs(workspaceRoot);
    indexWorkspaceCode(workspaceRoot);
  }

  return {
    capabilities: {
      textDocumentSync: TextDocumentSyncKind.Incremental,
      completionProvider: {
        triggerCharacters: ["."]
      },
      signatureHelpProvider: {
        triggerCharacters: ["(", ","]
      }
    }
  };
});

function getMembersForIdentifier(identifier: string, text: string): MemberInfo[] {
  if (typeMembers.has(identifier)) {
    return Array.from(typeMembers.get(identifier)!.values());
  }

  const varTypes = collectVarTypes(text);
  const resolvedType = varTypes.get(identifier);
  if (!resolvedType) {
    return [];
  }

  const members = typeMembers.get(resolvedType);
  return members ? Array.from(members.values()) : [];
}

connection.onCompletion((params: TextDocumentPositionParams) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) {
    return [];
  }

  const pos = params.position;
  const lineText = doc.getText({
    start: { line: pos.line, character: 0 },
    end: { line: pos.line, character: pos.character }
  });

  const match = /([A-Za-z_][A-Za-z0-9_]*)\.$/.exec(lineText);
  if (!match) {
    return [];
  }

  const identifier = match[1];
  const members = getMembersForIdentifier(identifier, doc.getText());
  return members.map((member) => {
    const item: CompletionItem = {
      label: member.name,
      kind: member.kind,
      detail: member.detail
    };
    return item;
  });
});

connection.onSignatureHelp((params): SignatureHelp | null => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) {
    return null;
  }

  const pos = params.position;
  const lineText = doc.getText({
    start: { line: pos.line, character: 0 },
    end: { line: pos.line, character: pos.character }
  });

  const callMatch = /([A-Za-z_][A-Za-z0-9_]*)(?:\.([A-Za-z_][A-Za-z0-9_]*))?\s*\(([^()]*)$/.exec(lineText);
  if (!callMatch) {
    return null;
  }

  const objectOrFunc = callMatch[1];
  const methodName = callMatch[2];
  const argsPrefix = callMatch[3];
  const activeParam = argsPrefix.trim() ? argsPrefix.split(",").length - 1 : 0;

  let signature: MemberInfo | undefined;
  if (methodName) {
    const members = getMembersForIdentifier(objectOrFunc, doc.getText());
    signature = members.find((member) => member.name === methodName);
  } else {
    signature = globalFunctions.get(objectOrFunc);
  }

  if (!signature) {
    return null;
  }

  const label = signature.detail;
  const parameters = signature.args.map((arg) => ({ label: arg }));
  const signatureInfo: SignatureInformation = { label, parameters };

  return {
    signatures: [signatureInfo],
    activeSignature: 0,
    activeParameter: Math.max(0, activeParam)
  };
});

documents.onDidChangeContent((change) => {
  const text = change.document.getText();
  parsePrototypeBlocks(text);
  parseGlobalFunctions(text);
});

documents.listen(connection);
connection.listen();
