(*
RUN: %plang_ir -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p;
begin end.

(*
CHECK-DAG: target datalayout
CHECK-DAG: i64:64
*)
