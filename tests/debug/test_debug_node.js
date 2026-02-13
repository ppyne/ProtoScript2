"use strict";

const debug = require("../../debug_node");

const makeProtoEnv = () => {
  const protos = new Map();
  protos.set("ProtoA", {
    name: "ProtoA",
    parent: null,
    sealed: true,
    fields: [
      { name: "a", type: { kind: "PrimitiveType", name: "int" } },
      { name: "name", type: { kind: "PrimitiveType", name: "string" } },
      { name: "self", type: { kind: "NamedType", name: "ProtoA" } },
    ],
    methods: new Map([
      [
        "foo",
        {
          name: "foo",
          params: [{ name: "x", type: { kind: "PrimitiveType", name: "int" }, variadic: false }],
          retType: { kind: "PrimitiveType", name: "int" },
        },
      ],
      [
        "bar",
        {
          name: "bar",
          params: [{ name: "vals", type: { kind: "PrimitiveType", name: "int" }, variadic: true }],
          retType: { kind: "PrimitiveType", name: "void" },
        },
      ],
    ]),
  });
  protos.set("ProtoB", {
    name: "ProtoB",
    parent: "ProtoA",
    sealed: false,
    fields: [{ name: "b", type: { kind: "PrimitiveType", name: "bool" } }],
    methods: new Map([
      [
        "foo",
        {
          name: "foo",
          params: [{ name: "x", type: { kind: "PrimitiveType", name: "int" }, variadic: false }],
          retType: { kind: "PrimitiveType", name: "int" },
        },
      ],
      [
        "baz",
        {
          name: "baz",
          params: [
            { name: "s", type: { kind: "PrimitiveType", name: "string" }, variadic: false },
            { name: "g", type: { kind: "PrimitiveType", name: "glyph" }, variadic: false },
          ],
          retType: { kind: "PrimitiveType", name: "string" },
        },
      ],
    ]),
  });
  protos.set("ProtoC", {
    name: "ProtoC",
    parent: "ProtoB",
    sealed: false,
    fields: [{ name: "c", type: { kind: "PrimitiveType", name: "float" } }],
    methods: new Map([
      [
        "baz",
        {
          name: "baz",
          params: [
            { name: "s", type: { kind: "PrimitiveType", name: "string" }, variadic: false },
            { name: "g", type: { kind: "PrimitiveType", name: "glyph" }, variadic: false },
          ],
          retType: { kind: "PrimitiveType", name: "string" },
        },
      ],
    ]),
  });
  protos.set("Simple", {
    name: "Simple",
    parent: null,
    sealed: false,
    fields: [
      { name: "x", type: { kind: "PrimitiveType", name: "int" } },
      { name: "y", type: { kind: "PrimitiveType", name: "string" } },
    ],
    methods: new Map([
      [
        "ping",
        {
          name: "ping",
          params: [],
          retType: { kind: "PrimitiveType", name: "int" },
        },
      ],
    ]),
  });
  protos.set("SealedChild", {
    name: "SealedChild",
    parent: "Simple",
    sealed: true,
    fields: [{ name: "z", type: { kind: "PrimitiveType", name: "int" } }],
    methods: new Map(),
  });
  protos.set("P", {
    name: "P",
    parent: null,
    sealed: true,
    fields: [{ name: "v", type: { kind: "PrimitiveType", name: "int" } }],
    methods: new Map([
      [
        "init",
        {
          name: "init",
          params: [],
          retType: { kind: "PrimitiveType", name: "void" },
        },
      ],
    ]),
  });
  protos.set("JSONValue", {
    name: "JSONValue",
    parent: null,
    sealed: true,
    fields: [],
    methods: new Map([
      ["isNull", { name: "isNull", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["isBool", { name: "isBool", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["isNumber", { name: "isNumber", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["isString", { name: "isString", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["isArray", { name: "isArray", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["isObject", { name: "isObject", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["asBool", { name: "asBool", params: [], retType: { kind: "PrimitiveType", name: "bool" } }],
      ["asNumber", { name: "asNumber", params: [], retType: { kind: "PrimitiveType", name: "float" } }],
      ["asString", { name: "asString", params: [], retType: { kind: "PrimitiveType", name: "string" } }],
      ["asArray", { name: "asArray", params: [], retType: { kind: "GenericType", name: "list", args: [{ kind: "NamedType", name: "JSONValue" }] } }],
      ["asObject", { name: "asObject", params: [], retType: { kind: "GenericType", name: "map", args: [{ kind: "PrimitiveType", name: "string" }, { kind: "NamedType", name: "JSONValue" }] } }],
    ]),
  });
  return protos;
};

const protoEnv = makeProtoEnv();
const groupEnv = new Map();
groupEnv.set("Color", {
  name: "Color",
  baseType: "int",
  members: [
    { name: "Black", literalType: "int", value: "0", runtimeValue: 0n },
    { name: "Cyan", literalType: "int", value: "65535", runtimeValue: 65535n },
  ],
});
groupEnv.get("Color").descriptor = {
  __group_desc: true,
  name: "Color",
  baseType: "int",
  members: groupEnv.get("Color").members,
};

const dump = (v) => debug.dump(v, { protoEnv, groups: groupEnv });
const makeJsonValue = (type, value) => ({ __json: true, type, value });

process.stderr.write("-- scalars --\n");
dump(true);
dump(255n);
dump(1234n);
dump(3.5);
dump({ __glyph_proxy: true, value: 0x41 });
dump("Hello");
dump("a".repeat(205));

process.stderr.write("-- collections --\n");
const listSmall = [1n, 2n, 3n];
listSmall.__type = "list<int>";
dump(listSmall);

process.stderr.write("-- typed list literal --\n");
const listLit = [0n, 1n, 2n, 3n];
listLit.__type = "list<int>";
dump(listLit);

const listByte = [0n, 255n];
listByte.__type = "list<byte>";
dump(listByte);

const listString = ["a", "b"];
listString.__type = "list<string>";
dump(listString);

const mapSmall = new Map();
mapSmall.__type = "map<string,int>";
mapSmall.set("string:a", 1n);
mapSmall.set("string:b", 2n);
dump(mapSmall);

const listViewSrc = [10n, 11n, 12n, 13n];
listViewSrc.__type = "list<int>";
const view = { __view: true, source: listViewSrc, offset: 1, len: 2, readonly: true, version: 0 };
view.__type = "view<int>";
dump(view);

const listLarge = [];
listLarge.__type = "list<int>";
for (let i = 0; i < 105; i += 1) listLarge.push(BigInt(i));
dump(listLarge);

const list0 = [];
const list1 = [];
const list2 = [];
const list3 = [];
const list4 = [];
const list5 = [];
const list6 = [];
list0.__type = "list<int>";
list1.__type = "list<int>";
list2.__type = "list<int>";
list3.__type = "list<int>";
list4.__type = "list<int>";
list5.__type = "list<int>";
list6.__type = "list<int>";
list6.push(42n);
list5.push(list6);
list4.push(list5);
list3.push(list4);
list2.push(list3);
list1.push(list2);
list0.push(list1);
dump(list0);

process.stderr.write("-- cycles --\n");
const listCyc = [];
listCyc.__type = "list<unknown>";
listCyc.push(listCyc);
dump(listCyc);

const mapCyc = new Map();
mapCyc.__type = "map<string,map>";
mapCyc.set("string:self", mapCyc);
dump(mapCyc);

process.stderr.write("-- groups --\n");
dump(65535n);
dump(groupEnv.get("Color").descriptor);

process.stderr.write("-- native json --\n");
dump(makeJsonValue("null", null));
dump(makeJsonValue("bool", true));
dump(makeJsonValue("number", 2.25));
dump(makeJsonValue("string", "Hello"));
dump(makeJsonValue("array", [makeJsonValue("null", null), makeJsonValue("bool", false)]));
const listJson = [makeJsonValue("null", null), makeJsonValue("bool", false)];
listJson.__type = "list<JSONValue>";
dump(listJson);
const objJson = new Map();
objJson.set("null", makeJsonValue("null", null));
dump(makeJsonValue("object", objJson));
const mapJson = new Map();
mapJson.__type = "map<string,JSONValue>";
mapJson.set("string:a", makeJsonValue("null", null));
dump(mapJson);

process.stderr.write("-- object --\n");
const obj = { __object: true, __proto: "ProtoB", __fields: Object.create(null) };
obj.__fields.a = 1234n;
obj.__fields.b = true;
dump(obj);

process.stderr.write("-- proto chain --\n");
const objc = { __object: true, __proto: "ProtoC", __fields: Object.create(null) };
objc.__fields.a = 1234n;
objc.__fields.b = true;
objc.__fields.c = 1.25;
dump(objc);

process.stderr.write("-- proto simple --\n");
const simple = { __object: true, __proto: "Simple", __fields: Object.create(null) };
simple.__fields.x = 10n;
simple.__fields.y = "first";
dump(simple);
simple.__fields.y = "second";
dump(simple);

process.stderr.write("-- proto sealed --\n");
const sobj = { __object: true, __proto: "SealedChild", __fields: Object.create(null) };
sobj.__fields.z = 7n;
dump(sobj);

process.stderr.write("-- determinism --\n");
dump(simple);
dump(simple);

process.stderr.write("-- repro --\n");
const pobj = { __object: true, __proto: "P", __fields: Object.create(null) };
dump(pobj);
pobj.__fields.v = 42n;
dump(pobj);
