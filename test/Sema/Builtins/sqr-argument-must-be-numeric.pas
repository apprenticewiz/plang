(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #261: same gap as abs, one builtin over -- see
   abs-argument-must-be-numeric.pas. *)
program p; var c1, c2: char; begin c1 := 'a'; c2 := sqr(c1) end.

(*
CHECK: 'sqr' requires a numeric argument, got 'char'
*)
