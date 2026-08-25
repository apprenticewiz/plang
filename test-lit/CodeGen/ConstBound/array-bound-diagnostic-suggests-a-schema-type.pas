(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
var n: integer;
    a: array[1..n] of integer;
begin end.

(*
CHECK: schema type
*)
