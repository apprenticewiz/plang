(*
CallStmt::ResolvedType's own reason for existing: A is called as a
statement (its result discarded) INSIDE B, textually before A's own
definition and with no 'forward' -- FreeDeclarationOrder (already Turbo's,
see declaration-sections-may-appear-in-any-order-and-be-repeated.pas)
lets Sema resolve A there without one.  CodeGen has not yet created A's
llvm::Function when it emits B's call to A
(CodeGenProcs.cpp's emitAllProcedures pre-declares only 'forward' procs),
so CGProcCall::emitUserProcCall's not-yet-defined-in-this-module fallback
runs and has to invent A's return type.  Before this had a real Sema-
resolved type to read (CallStmt::ResolvedType), it hardcoded void --
which worked here in isolation, but LEFT A's real, later definition
unable to reuse that (now type-mismatched) declaration: LLVM auto-
renamed A's actual body to 'pas_A.1', leaving the ORIGINAL 'pas_A' the
statement call site still targets permanently undefined, an
"undefined symbol: pas_A" link failure -- reproduced by hand while
developing this fix (a temporary revert of CallStmt::ResolvedType's
propagation) and fixed by giving the fallback A's real type instead.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

program xplusnoforward;
function B: integer;
begin
  A;
  B := 1;
end;
function A: integer;
begin
  A := 2;
end;
begin
  writeln(B);
end.

(*
CHECK:1
*)
