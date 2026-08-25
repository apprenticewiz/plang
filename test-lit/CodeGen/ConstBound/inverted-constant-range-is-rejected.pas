(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
var a: array[5..1] of integer;
begin end.

(*
CHECK: exceeds upper bound
*)
