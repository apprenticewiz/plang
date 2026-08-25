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
CHECK-NOT: !DILocalVariable(name: "localn"
CHECK-DAG: !DILocalVariable(name: "localN"
*)
