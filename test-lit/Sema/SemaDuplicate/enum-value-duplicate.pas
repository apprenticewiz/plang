(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p; type C = (red, red); begin end.

(*
CHECK: red
*)
