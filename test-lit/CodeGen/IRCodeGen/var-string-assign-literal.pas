(*
RUN: %plang -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p; var s: string(20); begin s := 'hello' end.

(*
CHECK-DAG: plang_str_from_cstr
CHECK-NOT: store ptr @
*)
