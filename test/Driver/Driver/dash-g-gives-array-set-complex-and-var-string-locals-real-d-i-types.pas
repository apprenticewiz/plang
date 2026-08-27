(*
RUN: %plang_ir -g -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
type arr = array[1..5] of integer;
var
  a: arr;
  s: set of 1..10;
  c: complex;
  vs: string(10);
begin
  a[1] := 7;
  s := [1, 3, 5];
  c := cmplx(1.0, 2.0);
  vs := 'hello';
  writeln(a[1])
end.

(*
CHECK-DAG: !DIGlobalVariable(name: "a", scope:
CHECK-DAG: !DICompositeType(tag: DW_TAG_array_type, baseType:
CHECK-DAG: !DIGlobalVariable(name: "s", scope:
CHECK-DAG: !DIBasicType(name: "byte", size: 8, encoding: DW_ATE_unsigned_char)
CHECK-DAG: !DIGlobalVariable(name: "c", scope:
CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "complex", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "re", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "im", scope:
CHECK-DAG: !DIGlobalVariable(name: "vs", scope:
CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "string", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "length", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "data", scope:
*)
