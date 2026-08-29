(*
The null-Type audit's own crash-prevention target, made concrete: a
procedural PARAMETER whose own formal is untyped (`procedure f(var x)`,
ISO §6.6.3.1's parameter form -- resolved through SemaType.cpp's
ProcedureTypeNode arm, which calls resolveParamType(*Pg.Type, ...) and used
to dereference a null Pg.Type unconditionally) is passed a procedure whose
matching parameter IS typed, so congruousSignature (SemaExpr.cpp) rejects
it and reports BOTH signatures through describeCallable (Sema/Type.h) --
which used to print "?" for a null Ty the same way it does for a genuine
resolution failure, or would have if Params[I].Ty->Name had been
dereferenced directly instead.  A naive implementation crashes just
DECLARING Higher's own parameter list, before this call is ever reached;
this test proves it does not, AND that the printed signature reads
"untyped" -- an actual, correct answer, not a masked failure.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
procedure Typed(var x: Integer);
begin
end;

procedure Higher(procedure f(var x));
begin
end;

begin
  Higher(Typed);
end.

(*
CHECK: 'Typed' has signature 'procedure(var integer)', which is not congruous with parameter 'f' of type 'procedure(var untyped)'
*)
