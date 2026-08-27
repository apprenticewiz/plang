(*
EP §6.4.7: a schema-typed variable's real extent is only known once its
discriminants are bound.  With a compile-time-constant discriminant
(vec(5) below) codegen lays it out for real and the DIType this produces
is exact; see CGDebugInfo::debugTypeOfSemaType's own Schema/SchemaInstance
case for the documented, deliberately-approximate case (an undiscriminated
schema, or one whose discriminant is not known until run time) that this
test does not exercise.
*)

(*
RUN: %plang_ir -g -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
type vec(n: integer) = record a: array[1..n] of integer end;
var v: vec(5);
begin
  v.a[1] := 3;
  writeln(v.a[1])
end.

(*
CHECK-DAG: !DIGlobalVariable(name: "v", scope:
CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "record (a)", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "a", scope:
CHECK-DAG: !DICompositeType(tag: DW_TAG_array_type, baseType:
*)
