(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p(output);
var i: integer;
begin i := 1; case i of 1: ; else ; end end.

(*
CHECK: an 'otherwise' part of a case-statement is an Extended Pascal extension
*)
