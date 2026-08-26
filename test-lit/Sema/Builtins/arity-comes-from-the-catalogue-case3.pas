(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p; var i: integer; begin i := succ(1, 2, 3) end.

(*
CHECK: 'succ' expects 1 or 2 argument(s), got 3
*)
