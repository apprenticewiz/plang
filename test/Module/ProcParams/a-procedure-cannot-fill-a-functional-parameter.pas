(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
function ap(function f(x: integer): integer): integer;
begin ap := f(1) end;
procedure noret(x: integer); begin end;
begin writeln(ap(noret)) end.

(*
CHECK: not congruous
*)
