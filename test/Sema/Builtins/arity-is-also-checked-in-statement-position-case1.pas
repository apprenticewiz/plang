(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #207: checkCallStmt's Builtin arm never called checkBuiltinArity the
   way checkCallExpr does, so a required PROCEDURE called with the wrong
   number of arguments reached codegen instead of being turned away here.
   reset's shape is file, plus an optional external filename -- a third
   argument reached a runtime call already typed for two, which the IR
   verifier rejected as a compiler crash rather than a diagnostic. *)

program p; var f: text; begin reset(f, 1, 2) end.

(*
CHECK: 'reset' expects 1 or 2 argument(s), got 3
*)
