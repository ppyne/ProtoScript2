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

const listByte = [0n, 255n];
listByte.__type = "list<byte>";
dump(listByte);

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

process.stderr.write("-- object --\n");
const obj = { __object: true, __proto: "ProtoB", __fields: Object.create(null) };
obj.__fields.a = 1234n;
obj.__fields.b = true;
dump(obj);
