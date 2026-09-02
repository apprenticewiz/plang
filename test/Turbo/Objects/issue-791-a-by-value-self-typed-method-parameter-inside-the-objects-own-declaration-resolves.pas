(*
Issue #791: a method parameter naming the ENCLOSING object type by value
(`procedure Copy(Other: TFoo)` inside TFoo's own declaration) used to be
refused with err_forward_type_reference -- Phase 3a only gives TFoo's name a
still-unresolved stub in the symbol table until resolveObjectType (SemaType.
cpp) returns, and the parameter lookup ran well before that. Confirmed
against a local `fpc -Mtp` build: 0 errors, compiles and runs correctly.

Fix: resolveObjectType now publishes its own in-progress `Type` (mutated in
place as it walks members) through PendingObjectType_ for exactly the
window it resolves a METHOD PARAMETER's type, and resolveNamed consults it
before raising the forward-reference error. See PendingObjectType_'s own
comment (Sema.h) for why no later patching is needed the way a pointer's
domain type gets it.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program SelfParamByValue;
type
  TFoo = object
    X: Integer;
    procedure Copy(Other: TFoo);
  end;
procedure TFoo.Copy(Other: TFoo);
begin
  X := Other.X;
end;
var a, b: TFoo;
begin
  a.X := 5;
  b.Copy(a);
  writeln(b.X);
end.

(*
CHECK:5
*)
