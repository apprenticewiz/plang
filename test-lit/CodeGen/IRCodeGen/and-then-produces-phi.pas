(*
RUN: %plang -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p; var a, b, c: boolean;
begin c := a and_then b end.

(*
CHECK-DAG: phi i1
CHECK-NOT:  and i1 
*)
