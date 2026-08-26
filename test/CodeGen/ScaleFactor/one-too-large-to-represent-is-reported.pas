(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p(output);
begin write(1.0e100000) end.

(*
CHECK: out of range
*)
