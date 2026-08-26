(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p; const n = undefined_const; begin end.

(*
CHECK: undefined_const
*)
