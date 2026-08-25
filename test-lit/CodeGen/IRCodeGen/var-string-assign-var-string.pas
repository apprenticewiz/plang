(*
RUN: %plang_ir -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p; var s, t: string(20); begin t := 'hi'; s := t end.

(*
CHECK-DAG: plang_str_assign
*)
