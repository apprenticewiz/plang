(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p; var x : integer; begin 99: x := 1 end.

(*
CHECK: 99
*)
