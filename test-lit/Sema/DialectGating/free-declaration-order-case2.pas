(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p(output);
var a: integer;
const c = 1;
begin a := c end.

(*
CHECK: expected 'begin', got 'const'
*)
