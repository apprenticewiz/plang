(*
{$X+} applies to a REQUIRED (builtin) function exactly the same as a
user-declared one -- Sema::checkCallStmt's own Sym->IsFunction arm, gated
identically to checkUserDefinedCall's.  Also exercises
CGFuncCall::emitBuiltinCall (factored out of emitCallExpr so
CGProcCall::emitCallStmt's tail -- reached once every required-PROCEDURE
name it dispatches by spelling has failed to match -- can call through to
the same builtin dispatch chain a call in expression position uses,
instead of falling into emitUserProcCall's user-defined-procedure path,
which has no declaration for a builtin at all).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

program xplusbuiltin;
begin
  Abs(-5);
  writeln('after the discarded Abs call');
end.

(*
CHECK:after the discarded Abs call
*)
