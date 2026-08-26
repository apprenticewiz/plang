(*
RUN: %plang_ir -O2 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p;
function work(n: integer): integer;
var i, acc: integer;
begin acc := 0; for i := 1 to n do acc := acc + i; work := acc end;
begin writeln(work(10)) end.

(*
CHECK-NOT: alloca
*)
