(*
Issue #509: the bare 'inherited' form (no method name, no argument list)
works as a VALUE too, not just as its own statement -- "the same method
this activation itself overrides, called with the same arguments this
activation itself received" (see InheritedCallStmt's own comment,
AstStmt.h), with the CALL's OWN RESULT now usable directly rather than
discarded.  TDog.GetName takes no arguments (so there is nothing to forward
here beyond Self, unlike bare-inherited-forwards-this-activations-own-
arguments.pas's own N: string parameter -- that sibling test already covers
argument forwarding for the STATEMENT form; this one is about the VALUE
coming back), reads TAnimal.GetName's own result through bare 'inherited',
and builds its own return value from it.

Cross-checked against a local `fpc -Mtp` build of this exact program: same
output, 'Rex the dog'.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program bareinheritedexpr;
type
  TAnimal = object
    function GetName: string;
  end;
  TDog = object(TAnimal)
    function GetName: string;
  end;

function TAnimal.GetName: string;
begin
  GetName := 'Rex';
end;

function TDog.GetName: string;
var
  S: string;
begin
  S := inherited;
  GetName := S + ' the dog';
end;

var
  D: TDog;
begin
  WriteLn(D.GetName());
end.

(*
CHECK:Rex the dog
*)
