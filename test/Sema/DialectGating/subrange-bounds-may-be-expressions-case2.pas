(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p(output);
const n = 5;
type t = 1..n+1;
var x: t;
begin x := 1 end.

(*
CHECK: expected ';', got '+'
*)
