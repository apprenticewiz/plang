(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p; var x : boolean; begin x := not 5 end.

(*
CHECK: not
*)
