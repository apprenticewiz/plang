(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p; var i: integer; begin i := abs() end.

(*
CHECK: 'abs' expects 1 argument(s), got 0
*)
