(*
RUN: %plang -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p; var s: string(20); b: boolean;
begin b := s = 'hello' end.

(*
CHECK-DAG: plang_str_from_cstr
CHECK-DAG: plang_str_eq
*)
