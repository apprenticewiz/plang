(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #306's fourth slice: same gap as get/put, confirmed to be a real,
   reproducible memory-safety bug on the pre-migration compiler -- compiling
   and running `close(i)` for a plain integer i segfaulted rather than
   merely computing something silently wrong, since CodeGen's
   FileVarHelpers::fileVarPtr handed the runtime i's own storage address
   reinterpreted as a PascalFile*.  See get-argument-must-be-a-file.pas for
   the full explanation; close shares the identical arm-less gap. *)
program p; var i: integer; begin i := 5; close(i) end.

(*
CHECK: 'close' requires a file argument, got 'integer'
*)
