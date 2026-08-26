(*
RUN: %plang_ir -g -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
procedure consume(var n: integer);
var a: integer;
begin
  a := n;
  writeln(a)
end;

begin
end.

(*
CHECK-NOT: #dbg_declare(ptr %n.param,
CHECK-DAG: store ptr %n.param, ptr %n.dbg,
CHECK-DAG: #dbg_declare(ptr %n.dbg, 
CHECK-DAG: !DIExpression(DW_OP_deref)
*)
