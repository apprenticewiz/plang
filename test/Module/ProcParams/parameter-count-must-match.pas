(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
function ap(function f(x: integer): integer): integer;
begin ap := f(1) end;
function bad(x, y: integer): integer; begin bad := 1 end;
begin writeln(ap(bad)) end.

(*
CHECK: not congruous
*)
