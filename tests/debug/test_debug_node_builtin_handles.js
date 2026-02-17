"use strict";

const debug = require("../../debug_node");

const protoEnv = new Map();
protoEnv.set("TextFile", {
  name: "TextFile",
  parent: null,
  sealed: true,
  fields: [],
  methods: new Map([
    ["read", { name: "read", params: [{ name: "size", type: { kind: "PrimitiveType", name: "int" }, variadic: false }], retType: { kind: "PrimitiveType", name: "string" } }],
    ["write", { name: "write", params: [{ name: "text", type: { kind: "PrimitiveType", name: "string" }, variadic: false }], retType: { kind: "PrimitiveType", name: "void" } }],
    ["tell", { name: "tell", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
    ["seek", { name: "seek", params: [{ name: "pos", type: { kind: "PrimitiveType", name: "int" }, variadic: false }], retType: { kind: "PrimitiveType", name: "void" } }],
    ["size", { name: "size", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
    ["name", { name: "name", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
    ["close", { name: "close", params: [], retType: { kind: "PrimitiveType", name: "void" } }],
  ]),
});

protoEnv.set("BinaryFile", {
  name: "BinaryFile",
  parent: null,
  sealed: true,
  fields: [],
  methods: new Map([
    ["read", { name: "read", params: [{ name: "size", type: { kind: "PrimitiveType", name: "int" }, variadic: false }], retType: { kind: "GenericType", name: "list", args: [{ kind: "PrimitiveType", name: "byte" }] } }],
    ["write", { name: "write", params: [{ name: "bytes", type: { kind: "GenericType", name: "list", args: [{ kind: "PrimitiveType", name: "byte" }] }, variadic: false }], retType: { kind: "PrimitiveType", name: "void" } }],
    ["tell", { name: "tell", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
    ["seek", { name: "seek", params: [{ name: "pos", type: { kind: "PrimitiveType", name: "int" }, variadic: false }], retType: { kind: "PrimitiveType", name: "void" } }],
    ["size", { name: "size", params: [], retType: { kind: "PrimitiveType", name: "int" } }],
    ["name", { name: "name", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
    ["close", { name: "close", params: [], retType: { kind: "PrimitiveType", name: "void" } }],
  ]),
});

protoEnv.set("Dir", {
  name: "Dir",
  parent: null,
  sealed: false,
  fields: [],
  methods: new Map([
    ["hasNext", { name: "hasNext", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
    ["next", { name: "next", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
    ["close", { name: "close", params: [], retType: { kind: "PrimitiveType", name: "void" } }],
    ["reset", { name: "reset", params: [], retType: { kind: "PrimitiveType", name: "void" } }],
  ]),
});

protoEnv.set("Walker", {
  name: "Walker",
  parent: null,
  sealed: false,
  fields: [],
  methods: new Map([
    ["hasNext", { name: "hasNext", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
    ["next", { name: "next", params: [], retType: { kind: "NamedType", name: "PathEntry" } }],
    ["close", { name: "close", params: [], retType: { kind: "PrimitiveType", name: "void" } }],
  ]),
});

const dump = (v) => debug.dump(v, { protoEnv });

process.stderr.write("-- builtin handles --\n");
dump({ constructor: { name: "TextFile" }, fd: 1, closed: false, isStd: true, path: "stdout" });
dump({ constructor: { name: "BinaryFile" }, fd: 3, closed: true, isStd: false, path: "/tmp/a.bin" });
dump({ __fs_dir: true, path: ".", closed: false, done: false });
dump({ __fs_walker: true, root: ".", maxDepth: -1, followSymlinks: false, closed: false });
