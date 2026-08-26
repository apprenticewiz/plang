(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p; var x : integer; begin x := 1 div 1.5 end.

(*
CHECK: requires integer operands
*)
