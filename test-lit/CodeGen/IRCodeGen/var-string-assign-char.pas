(*
RUN: %plang_ir -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p; var s: string(10); c: char;
begin c := 'x'; s := c end.

(*
CHECK-DAG: plang_str_from_char
*)
