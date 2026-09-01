(*
Turbo Tier 5, Cluster B item 8 -- regression coverage for a real bug found
in independent review of this item's own first landing: a cross-unit
method call whose callee had not yet been declared in this translation
unit (i.e. genuinely reached only through CGProcCall::emitMethodCallStmt's
or CGFuncCall::emitMethodCallExpr's own "not found -- declare it" fallback)
used to guess the callee's own parameter types from the CALL SITE's
argument expressions instead of from the callee's own real, resolved
signature. That guess is only ever WRONG when an argument's own static
type is merely assignment-COMPATIBLE with, rather than IDENTICAL to, the
formal's real type -- which a scalar Integer argument never exposes (an
Integer actual for an Integer formal has no other type to be), but a
STRING one does: a string LITERAL's own static type
(TypeKind::String, lowered to a bare `ptr`) differs from a `string`
formal's real type (TypeKind::ShortString, lowered to a `{i8,[255 x
i8]}` struct), so the guessed declaration and the real callee disagreed,
and StringCallMarshalling::emitCallArg -- reading the (wrong) declared
type back at the call -- sent the literal through a code path that
returns null for it, an LLVM IR verifier "Operand is null" on every
cross-unit method call passing a string actual. This is deliberately a
narrower, more targeted regression guard than the tier's own 3-unit
inheritance capstone (three-units-cross-unit-object-inheritance-and-
virtual-dispatch.pas, which now also carries a string-argument method
call of its own) -- no inheritance, no virtual dispatch, a single
`uses`, so a future failure here points straight at cross-unit method
PARAMETER MARSHALLING specifically, not at the ancestor-chain machinery.

Exercises both call shapes that each keep their own declare-if-missing
fallback (CGProcCall::emitMethodCallStmt for a procedure-method called as
a statement, CGFuncCall::emitMethodCallExpr for a function-method called
in expression position) -- a string literal actual for one, a string
variable actual for the other, so neither call site's own fix is trusted
on the strength of the other's.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/animalunit.pas -o %t.dir/animalunit.o
RUN: rm %t.dir/animalunit.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/prog.pas %t.dir/animalunit.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:Name: Rex
CHECK-NEXT:Greeting: Hello, Rex
CHECK-NEXT:Name: Fido
*)

//--- animalunit.pas
unit AnimalUnit;

interface

type
  TAnimal = object
    Name: string;
    procedure SetName(N: string);
    function Greeting: string;
  end;

implementation

procedure TAnimal.SetName(N: string);
begin
  Name := N;
end;

function TAnimal.Greeting: string;
begin
  Greeting := 'Hello, ' + Name;
end;

end.

//--- prog.pas
program Prog;

uses AnimalUnit;

var
  A: TAnimal;
  NewName: string;

begin
  A.SetName('Rex');                 { procedure-method, string LITERAL arg }
  writeln('Name: ', A.Name);
  { Explicit parens: a bare (no-parens) method call in expression position
    is refused even for a directly-declared, non-inherited method -- a
    separate, pre-existing Cluster A gap (unrelated to this item), routed
    around the same way the tier's own 3-unit capstone test already does. }
  writeln('Greeting: ', A.Greeting()); { function-method, no args, in expression position }

  NewName := 'Fido';
  A.SetName(NewName);               { procedure-method, string VARIABLE arg }
  writeln('Name: ', A.Name);
end.
