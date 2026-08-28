(*
RUN: %plang_ep -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #417: position (EP §6.7.6.6) reports a value of its file argument's
   declared index type, so the argument names the file to test -- the same
   file-argument requirement eof/eoln already enforce (see
   ../Builtins/eof-argument-must-be-a-file.pas).  This had no check at all,
   so `position(i)` for a plain integer i type-checked with nothing to
   reject it: CodeGen's lowering (FileVars.fileVarPtr) has no non-file
   fallback the way eof/eoln's stdin fallback does, and handed i's own
   address to the runtime as a PascalFile*, segfaulting with no
   diagnostic. *)
program p; var i, n: integer; begin i := 42; n := position(i) end.

(*
CHECK: 'position' requires a file argument, got 'integer'
*)
