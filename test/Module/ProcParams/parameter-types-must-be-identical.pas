(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
The message has to name both signatures, or it says nothing useful.
CHECK-DAG: not congruous
CHECK-DAG: function(real): integer
CHECK-DAG: function(integer): integer
*)

program p;
function ap(function f(x: integer): integer): integer;
begin ap := f(1) end;
function bad(x: real): integer; begin bad := 1 end;
begin writeln(ap(bad)) end.
