(*
RUN: %plang_ep -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #261: index takes two string-like arguments (EP §6.7.6.7), and
   like length (see length-argument-must-be-string-like.pas) had no check
   of its own. *)
program p; var i, n: integer; begin i := 42; n := index(i, i) end.

(*
CHECK: 'integer' cannot be an argument of index; it must be char or string
*)
