(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
var a: array['z'..'a'] of integer;
begin end.

(*
CHECK: 'z' exceeds upper bound 'a'
*)
