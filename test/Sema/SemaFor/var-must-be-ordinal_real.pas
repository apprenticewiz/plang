(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p; var x : real; begin for x := 1.0 to 5.0 do x := x end.

(*
CHECK: ordinal
*)
