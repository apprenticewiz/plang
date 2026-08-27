(*
RUN: %plang_ir -g -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
type rec = record a: integer; b: real end;
var r: rec;
begin
  r.a := 1; r.b := 2.0;
  writeln(r.a)
end.

(*
CHECK-DAG: !DIGlobalVariable(name: "r", scope:
CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "rec", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "a", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "b", scope:
*)
