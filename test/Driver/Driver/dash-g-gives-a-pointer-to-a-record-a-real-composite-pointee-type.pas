(*
RUN: %plang_ir -g -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
type rec = record f: integer end;
var ip: ^integer; rp: ^rec;
begin
  new(ip); ip^ := 1;
  new(rp); rp^.f := 2;
  writeln(ip^)
end.

(*
CHECK-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType:
CHECK-DAG: !DIBasicType(name: "integer", size: 64, encoding: DW_ATE_signed)
CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "rec",
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "f",
*)
