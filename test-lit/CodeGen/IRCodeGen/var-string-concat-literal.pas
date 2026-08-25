(*
RUN: %plang -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p; var s, u: string(40);
begin s := 'Hello'; u := s + ', World' end.

(*
CHECK-DAG: plang_str_concat
*)
