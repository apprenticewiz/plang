(*
RUN: %plang_ir -g -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
var x: integer;

procedure addone(var n: integer);
var doubled: integer;
begin
  doubled := n * 2;
  n := n + 1
end;

begin
  x := 10;
  addone(x);
  writeln(x)
end.

(*
CHECK-DAG: #dbg_declare
CHECK-DAG: !DILocalVariable(name: "n"
CHECK-DAG: !DILocalVariable(name: "doubled"
*)
