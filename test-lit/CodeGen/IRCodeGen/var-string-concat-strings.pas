(*
RUN: %plang -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p; var a, b, u: string(20);
begin a := 'foo'; b := 'bar'; u := a + b end.

(*
CHECK-DAG: plang_str_concat
CHECK-NOT: plang_str_concat_cstr
CHECK-NOT: plang_str_concat_char
*)
