(*
Issue #786: a qualified, no-parens call to a parameterless method that
returns `string` (e.g. `other.Name` where `function Name: string`) crashed
codegen with an LLVM IR verifier ICE ("object has no field named 'Name'").

Root cause: CGExprCore::emitExpr's ExprIsVarStr/ExprIsShortStr FieldExpr
branches ran BEFORE the IsImplicitMethodCall check, so a FieldExpr for an
implicit method call whose ResolvedType happened to be `string` was treated
as a genuine field access and routed into FieldAccess.emitFieldGEP, which
found no such field. Reproduces via a VAR-parameter receiver (the issue's
exact repro) and is exercised with and without an explicit pointer
dereference receiver.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program Min7;
type
  TBase = object
    function Name: string;
  end;
  PBase = ^TBase;
function TBase.Name: string; begin Name := 'Base'; end;
procedure ShowIt(var other: TBase);
begin
  writeln('Name=', other.Name);   { crashed here before the fix }
end;
var
  a: TBase;
  p: PBase;
begin
  ShowIt(a);
  p := @a;
  writeln('Name=', p^.Name);      { qualified through a pointer deref }
end.

(*
CHECK:Name=Base
CHECK-NEXT:Name=Base
*)
