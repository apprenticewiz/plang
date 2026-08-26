(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
var lo, hi: integer;
    a: array[lo..hi] of integer;
begin end.

(*
CHECK-DAG: lower bound
CHECK-DAG: upper bound
*)
