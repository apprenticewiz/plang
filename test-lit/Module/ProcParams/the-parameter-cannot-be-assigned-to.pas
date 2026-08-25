(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
function ap(function f(x: integer): integer): integer;
begin f := 3; ap := 1 end;
function d(x: integer): integer; begin d := x end;
begin writeln(ap(d)) end.

(*
CHECK: 'f' is a procedural parameter
*)
