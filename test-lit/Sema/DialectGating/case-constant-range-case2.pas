(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p(output);
var i: integer;
begin i := 1; case i of 1..3: ; end end.

(*
CHECK: a range as a case-constant is an Extended Pascal extension
*)
