(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p; type S = set of real; begin end.

(*
CHECK: ordinal
*)
