(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #306's fourth slice: Append's File-kind check is generic now
   (Builtins.def's AK_File row) -- split out of the hand-written arm it used
   to share with Assign (which stays hand-written: its own second argument,
   a filename, is a different kind). *)
program p; var i: integer; begin i := 5; append(i) end.

(*
CHECK: 'append' requires a file argument, got 'integer'
*)
