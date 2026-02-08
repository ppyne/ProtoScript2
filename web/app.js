/*const sample = `import Io;

function main() : void {
    Io.print("Hello world\\n");
}
`;*/

const sample = `import Io;

function main() : void {
    Io.printLine("Hello world");
}
`;

const outputEl = document.getElementById("output");
const sourceEl = document.getElementById("source");
const runBtn = document.getElementById("run");
const statusEl = document.getElementById("status");

let editor = null;

if (window.CodeMirror) {
  editor = CodeMirror.fromTextArea(sourceEl, {
    mode: "text/x-csrc",
    lineNumbers: true,
    lineWrapping: true,
  });
  editor.setValue(sample);
} else {
  sourceEl.value = sample;
}

runBtn.disabled = true;

const appendOut = (text) => {
  outputEl.textContent += text + "\n";
};

const clearOut = () => {
  outputEl.textContent = "";
};

let runtime = null;

const moduleConfig = {
  print: (text) => appendOut(text),
  printErr: (text) => appendOut(`[error] ${text}`),
  ENV: {
    PS_MODULE_REGISTRY: "/modules/registry.json",
  },
};

const registryJson = String.raw`{
  "modules": [
    {
      "name": "test.simple",
      "functions": [
        { "name": "add", "ret": "int", "params": ["int", "int"] }
      ]
    },
    {
      "name": "test.utf8",
      "functions": [
        { "name": "roundtrip", "ret": "string", "params": ["string"] }
      ]
    },
    {
      "name": "test.throw",
      "functions": [
        { "name": "fail", "ret": "void", "params": [] }
      ]
    },
    {
      "name": "test.noinit",
      "functions": [
        { "name": "ping", "ret": "int", "params": [] }
      ]
    },
    {
      "name": "test.badver",
      "functions": [
        { "name": "ping", "ret": "int", "params": [] }
      ]
    },
    {
      "name": "test.nosym",
      "functions": [
        { "name": "missing", "ret": "int", "params": [] }
      ]
    },
    {
      "name": "test.missing",
      "functions": [
        { "name": "ping", "ret": "int", "params": [] }
      ]
    },
    {
      "name": "test.invalid",
      "functions": [
        { "name": "bad", "ret": "badtype", "params": ["int"] }
      ]
    },
    {
      "name": "Math",
      "functions": [
        { "name": "abs", "ret": "float", "params": ["float"] },
        { "name": "min", "ret": "float", "params": ["float", "float"] },
        { "name": "max", "ret": "float", "params": ["float", "float"] },
        { "name": "floor", "ret": "float", "params": ["float"] },
        { "name": "ceil", "ret": "float", "params": ["float"] },
        { "name": "round", "ret": "float", "params": ["float"] },
        { "name": "sqrt", "ret": "float", "params": ["float"] },
        { "name": "pow", "ret": "float", "params": ["float", "float"] },
        { "name": "sin", "ret": "float", "params": ["float"] },
        { "name": "cos", "ret": "float", "params": ["float"] },
        { "name": "tan", "ret": "float", "params": ["float"] },
        { "name": "asin", "ret": "float", "params": ["float"] },
        { "name": "acos", "ret": "float", "params": ["float"] },
        { "name": "atan", "ret": "float", "params": ["float"] },
        { "name": "atan2", "ret": "float", "params": ["float", "float"] },
        { "name": "sinh", "ret": "float", "params": ["float"] },
        { "name": "cosh", "ret": "float", "params": ["float"] },
        { "name": "tanh", "ret": "float", "params": ["float"] },
        { "name": "asinh", "ret": "float", "params": ["float"] },
        { "name": "acosh", "ret": "float", "params": ["float"] },
        { "name": "atanh", "ret": "float", "params": ["float"] },
        { "name": "exp", "ret": "float", "params": ["float"] },
        { "name": "expm1", "ret": "float", "params": ["float"] },
        { "name": "log", "ret": "float", "params": ["float"] },
        { "name": "log1p", "ret": "float", "params": ["float"] },
        { "name": "log2", "ret": "float", "params": ["float"] },
        { "name": "log10", "ret": "float", "params": ["float"] },
        { "name": "cbrt", "ret": "float", "params": ["float"] },
        { "name": "hypot", "ret": "float", "params": ["float", "float"] },
        { "name": "trunc", "ret": "float", "params": ["float"] },
        { "name": "sign", "ret": "float", "params": ["float"] },
        { "name": "fround", "ret": "float", "params": ["float"] },
        { "name": "clz32", "ret": "float", "params": ["float"] },
        { "name": "imul", "ret": "float", "params": ["float", "float"] },
        { "name": "random", "ret": "float", "params": [] }
      ],
      "constants": [
        { "name": "PI", "type": "float", "value": "3.141592653589793" },
        { "name": "E", "type": "float", "value": "2.718281828459045" },
        { "name": "LN2", "type": "float", "value": "0.6931471805599453" },
        { "name": "LN10", "type": "float", "value": "2.302585092994046" },
        { "name": "LOG2E", "type": "float", "value": "1.4426950408889634" },
        { "name": "LOG10E", "type": "float", "value": "0.4342944819032518" },
        { "name": "SQRT1_2", "type": "float", "value": "0.7071067811865476" },
        { "name": "SQRT2", "type": "float", "value": "1.4142135623730951" }
      ]
    },
    {
      "name": "Io",
      "functions": [
        { "name": "open", "ret": "File", "params": ["string", "string"] },
        { "name": "print", "ret": "void", "params": ["string"] },
        { "name": "printLine", "ret": "void", "params": ["string"] }
      ],
      "constants": [
        { "name": "EOL", "type": "string", "value": "\\n" },
        { "name": "stdin", "type": "file", "value": "stdin" },
        { "name": "stdout", "type": "file", "value": "stdout" },
        { "name": "stderr", "type": "file", "value": "stderr" }
      ]
    },
    {
      "name": "JSON",
      "functions": [
        { "name": "encode", "ret": "string", "params": ["JSONValue"] },
        { "name": "decode", "ret": "JSONValue", "params": ["string"] },
        { "name": "isValid", "ret": "bool", "params": ["string"] },
        { "name": "null", "ret": "JSONValue", "params": [] },
        { "name": "bool", "ret": "JSONValue", "params": ["bool"] },
        { "name": "number", "ret": "JSONValue", "params": ["float"] },
        { "name": "string", "ret": "JSONValue", "params": ["string"] },
        { "name": "array", "ret": "JSONValue", "params": ["list<JSONValue>"] },
        { "name": "object", "ret": "JSONValue", "params": ["map<string,JSONValue>"] }
      ]
    }
  ]
}
`;

const ensureRegistry = () => {
  if (!runtime || !runtime.FS) return;
  const targets = [
    "modules/registry.json",
    "/modules/registry.json",
    "/home/web_user/modules/registry.json",
  ];
  for (const path of targets) {
    const dir = path.replace(/\/[^/]+$/, "");
    try {
      runtime.FS.stat(dir);
    } catch {
      try {
        runtime.FS.mkdir(dir);
      } catch {
        // ignore
      }
    }
    try {
      runtime.FS.stat(path);
    } catch {
      runtime.FS.writeFile(path, registryJson);
    }
  }
  try {
    runtime.FS.chdir("/");
  } catch {
    // ignore
  }
};

window.startProtoScript = () => {
  if (typeof ProtoScript !== "function") {
    appendOut("[error] protoscript.js did not load");
    return;
  }
  ProtoScript(moduleConfig).then((instance) => {
    runtime = instance;
    ensureRegistry();
    statusEl.textContent = "Runtime ready";
    runBtn.disabled = false;
  });
};

const runProgram = () => {
  if (!runtime || !runtime.FS || !runtime.callMain) {
    appendOut("[error] runtime not ready");
    return;
  }
  clearOut();
  statusEl.textContent = "Running...";
  runBtn.disabled = true;
  try {
    const code = editor ? editor.getValue() : sourceEl.value;
    ensureRegistry();
    runtime.FS.writeFile("/program.pts", code);
    runtime.callMain(["run", "/program.pts"]);
  } catch (err) {
    appendOut(`[exception] ${err}`);
  } finally {
    runBtn.disabled = false;
    statusEl.textContent = "Runtime ready";
  }
};

runBtn.addEventListener("click", runProgram);
