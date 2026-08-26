(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p; begin for i := 1 to 5 do i := i end.

(*
CHECK: i
*)
