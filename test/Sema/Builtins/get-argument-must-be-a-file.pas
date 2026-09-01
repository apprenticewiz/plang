(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #306's fourth slice: get had no argument-kind check of its own at
   all -- neither checkCallExpr nor checkCallStmt special-cases its name, so
   it fell through to the generic per-argument checkExpr walk with nothing
   to reject a non-file argument.  FileVarHelpers::fileVarPtr's IdentExpr
   fast path (CodeGen) hands back ANY variable's own storage address
   unchecked, so `get(i)` for a plain integer i reinterpreted i's storage as
   a PascalFile* at runtime and corrupted memory with no diagnostic -- the
   same bug class issue #417 closed for position/lastposition/empty and
   issue #261 closed for eof/eoln. *)
program p; var i: integer; begin i := 5; get(i) end.

(*
CHECK: 'get' requires a file argument, got 'integer'
*)
