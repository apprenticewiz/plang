(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
var n: integer;
procedure q;
type s = 1..n;
var x: s;
begin end;
begin end.

(*
CHECK: upper bound of subrange
*)
