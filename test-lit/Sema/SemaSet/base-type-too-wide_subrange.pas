(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p; type S = set of 0..256; begin end.

(*
CHECK: exceeds
*)
