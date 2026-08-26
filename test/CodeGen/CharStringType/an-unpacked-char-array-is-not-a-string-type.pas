(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p(output);
var s: array[1..5] of char;
begin s := 'hello'; writeln(s) end.

(*
CHECK: cannot be written
*)
