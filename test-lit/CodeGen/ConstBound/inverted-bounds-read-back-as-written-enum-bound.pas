(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
type c = (r, g, b);
var a: array[b..r] of integer;
begin end.

(*
CHECK: b exceeds upper bound r
*)
