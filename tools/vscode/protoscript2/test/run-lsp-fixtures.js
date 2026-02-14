const fs = require("fs");
const path = require("path");
const assert = require("assert");

const frontend = require("../../../../src/frontend.js");

const root = __dirname;
const fixturesDir = path.join(root, "fixtures");
const expectedDir = path.join(root, "expected");

const cases = [
  {
    kind: "completion",
    name: "completion_member_simple",
    fixture: "completion_member_simple.pts",
    expected: "completion_member_simple.json",
    position: { line: 8, character: 4 }
  },
  {
    kind: "completion",
    name: "completion_unknown",
    fixture: "completion_unknown.pts",
    expected: "completion_unknown.json",
    position: { line: 1, character: 4 }
  },
  {
    kind: "completion",
    name: "completion_self",
    fixture: "completion_self.pts",
    expected: "completion_self.json",
    position: { line: 3, character: 9 }
  },
  {
    kind: "hover",
    name: "hover_local",
    fixture: "hover_local.pts",
    expected: "hover_local.json",
    position: { line: 8, character: 2 }
  },
  {
    kind: "definition",
    name: "definition_member",
    fixture: "definition_member.pts",
    expected: "definition_member.json",
    position: { line: 8, character: 5 }
  },
  {
    kind: "completion",
    name: "preproc_define_simple",
    fixture: "preproc_define_simple.pts",
    expected: "preproc_define_simple.json",
    position: { line: 9, character: 4 }
  },
  {
    kind: "completion",
    name: "preproc_mid_prototype",
    fixture: "preproc_mid_prototype.pts",
    expected: "preproc_mid_prototype.json",
    position: { line: 11, character: 4 }
  },
  {
    kind: "diagnostics",
    name: "preproc_ifdef_mask",
    fixture: "preproc_ifdef_mask.pts",
    expected: "preproc_ifdef_mask_diagnostics.json"
  }
];

for (const testCase of cases) {
  const fixturePath = path.join(fixturesDir, testCase.fixture);
  const expectedPath = path.join(expectedDir, testCase.expected);
  const source = fs.readFileSync(fixturePath, "utf8");
  const expected = JSON.parse(fs.readFileSync(expectedPath, "utf8"));

  const model = frontend.buildSemanticModel(fixturePath, source, {});

  if (testCase.kind === "completion") {
    const actual = model.queries
      .completionsAt(testCase.position.line, testCase.position.character)
      .map((item) => item.label)
      .sort();
    assert.deepStrictEqual(actual, expected, `${testCase.name} failed`);
    continue;
  }

  if (testCase.kind === "hover") {
    const actual = model.queries.hoverAt(testCase.position.line, testCase.position.character);
    assert.deepStrictEqual(actual, expected, `${testCase.name} failed`);
    continue;
  }

  if (testCase.kind === "definition") {
    const actual = model.queries.definitionAt(testCase.position.line, testCase.position.character);
    assert.deepStrictEqual(actual, expected, `${testCase.name} failed`);
    continue;
  }

  if (testCase.kind === "diagnostics") {
    const actual = (model.diagnostics || [])
      .map((d) => ({
        code: d.code || d.name || "PS2",
        line: d.line
      }))
      .sort((a, b) => a.line - b.line || String(a.code).localeCompare(String(b.code)));
    assert.deepStrictEqual(actual, expected, `${testCase.name} failed`);
    continue;
  }

  throw new Error(`Unknown test kind: ${testCase.kind}`);
}

// Ensure the preprocessor path is fully wasm-based and does not depend on a system mcpp binary.
{
  const fixturePath = path.join(fixturesDir, "preproc_define_simple.pts");
  const source = fs.readFileSync(fixturePath, "utf8");
  const prevPath = process.env.PATH;
  process.env.PATH = "";
  try {
    const model = frontend.buildSemanticModel(fixturePath, source, {});
    const labels = model.queries.completionsAt(9, 4).map((item) => item.label).sort();
    assert.deepStrictEqual(labels, ["foo", "x"], "preprocess_no_system_mcpp failed");
  } finally {
    process.env.PATH = prevPath;
  }
}

console.log(`OK ${cases.length + 1} fixture tests passed`);
