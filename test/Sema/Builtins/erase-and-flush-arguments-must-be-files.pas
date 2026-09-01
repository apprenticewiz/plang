(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #306's fourth slice: Erase and Flush each take a file argument
   alone, and are generic now (Builtins.def's AK_File row) -- see
   get-argument-must-be-a-file.pas for the underlying FileVarHelpers::
   fileVarPtr memory-safety consequence a wrong-typed argument used to have. *)
program p; var i: integer; begin i := 5; erase(i); flush(i) end.

(*
CHECK: 'erase' requires a file argument, got 'integer'
CHECK: 'flush' requires a file argument, got 'integer'
*)
