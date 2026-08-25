(*
RUN: %plang_ir -g -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
procedure outer(n: integer);
  var localN: integer;
  procedure inner;
  begin
    localN := localN + 1
  end;
begin
  localN := n;
  inner;
  writeln(localN)
end;

begin
  outer(5)
end.

(*
CHECK-NOT: #dbg_declare(ptr %outer.localn,
CHECK-DAG: store ptr %outer.localn, ptr %localN.dbg,
CHECK-DAG: #dbg_declare(ptr %localN.dbg, 
CHECK-DAG: !DIExpression(DW_OP_deref)
*)
