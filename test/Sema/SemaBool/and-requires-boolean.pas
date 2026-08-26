(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p; var x : integer; begin x := 1 and 2 end.

(*
CHECK: and
*)
