(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #207: too FEW arguments took the same unchecked path -- reset()
   fell all the way through checkCallStmt's Builtin arm to emitUserProcCall,
   which emitted a call to an external symbol nothing defines rather than a
   "wrong number of arguments" diagnostic. *)

program p; begin reset() end.

(*
CHECK: 'reset' expects 1 or 2 argument(s), got 0
*)
