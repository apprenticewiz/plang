(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p; var x : integer; begin if 1 then x := 0 end.

(*
CHECK: boolean
*)
