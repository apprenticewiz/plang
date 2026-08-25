(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
function ap(function f(x: integer): integer): integer; forward;
function ap(function f(x: real): integer): integer;
begin ap := 1 end;
begin writeln(1) end.

(*
CHECK: does not match forward declaration
*)
