(*
RUN: %plang -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p; begin writeln('hello') end.

(*
CHECK-NOT: plang_str_from_cstr
CHECK-DAG: plang_writeln_str
*)
