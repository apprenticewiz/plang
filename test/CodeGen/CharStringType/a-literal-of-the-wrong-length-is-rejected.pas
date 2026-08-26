(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
var s: packed array[1..5] of char;
begin s := 'hi'; s := 'toolong' end.

(*
CHECK-DAG: string of length 2 does not fit
CHECK-DAG: string of length 7 does not fit
*)
