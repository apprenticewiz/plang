(*
RUN: %plang_ep -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #417: empty (EP §6.7.6.5) reports whether its file argument holds no
   components, so the argument names the file to test -- the same gap as
   position/lastposition, just above (see
   position-argument-must-be-a-file.pas), for a third builtin CodeGen has no
   non-file fallback for. *)
program p; var i: integer; b: boolean; begin i := 42; b := empty(i) end.

(*
CHECK: 'empty' requires a file argument, got 'integer'
*)
