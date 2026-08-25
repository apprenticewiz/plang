(*
RUN: not %plang_ir -std=iso10206 -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
var s: string(0);
begin end.

(*
CHECK: must be positive
*)
