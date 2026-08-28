(*
RUN: %plang_ep -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #417: same gap as position, one builtin over -- see
   position-argument-must-be-a-file.pas. *)
program p; var i, n: integer; begin i := 42; n := lastposition(i) end.

(*
CHECK: 'lastposition' requires a file argument, got 'integer'
*)
