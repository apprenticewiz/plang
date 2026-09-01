(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #306's fourth slice: same gap as get, one builtin over -- see
   get-argument-must-be-a-file.pas. *)
program p; var i: integer; begin i := 5; put(i) end.

(*
CHECK: 'put' requires a file argument, got 'integer'
*)
