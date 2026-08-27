(*
RUN: %plang_ir -g -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
function double(x: integer): integer;
begin double := x * 2 end;

procedure apply(function f(x: integer): integer; v: integer);
var r: integer;
begin
  r := f(v);
  writeln(r)
end;

begin
  apply(double, 21)
end.

(*
CHECK-DAG: #dbg_declare(ptr %f.closure
CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "procparam", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "code", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "frame", scope:
CHECK-DAG: !DISubroutineType(types:
*)
