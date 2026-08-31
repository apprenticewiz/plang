(*
Turbo Tier 5, Cluster A item 6: 'Fail' inside a constructor.  Confirmed
against a local `fpc -Mtp` build (see err_fail_outside_constructor's own
comment, DiagnosticSemaKinds.def, and curCtorOkAlloca's, CodeGenImpl.h) that
real Borland/FPC's contract is: the partially-constructed object is
deallocated, and New(P, Init(...))'s own P ends up nil -- 'Fail' unwinds
back through the New call itself, not merely the constructor.  No destructor
runs on the Fail path either way, confirmed even when an ancestor's own
portion of construction had already completed (the 'inherited Init' below
DOES run and set a field, and Fail is only reached afterward -- Done still
never runs).
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  PAnimal = ^TAnimal;
  TAnimal = object
    Name: string[20];
    constructor Init;
    destructor Done;
  end;
  PDog = ^TDog;
  TDog = object(TAnimal)
    constructor Init(OK: boolean);
    destructor Done;
  end;

constructor TAnimal.Init;
begin
  writeln('Animal.Init entered');
  Name := 'base';
end;

destructor TAnimal.Done;
begin
  writeln('Animal.Done running');
end;

constructor TDog.Init(OK: boolean);
begin
  writeln('Dog.Init entered');
  inherited Init;
  if not OK then
  begin
    writeln('Dog.Init calling Fail');
    Fail;
  end;
  Name := 'Rex';
  writeln('Dog.Init leaving normally');
end;

destructor TDog.Done;
begin
  writeln('Dog.Done running');
  inherited Done;
end;

var
  P: PDog;
begin
  New(P, Init(false));
  if P = nil then
    writeln('P is nil after failed Init')
  else
    writeln('P is NOT nil (WRONG)');

  New(P, Init(true));
  if P = nil then
    writeln('P is nil after successful Init (WRONG)')
  else
  begin
    writeln('P is not nil after successful Init');
    Dispose(P, Done);
  end;
end.

(*
CHECK:Dog.Init entered
CHECK-NEXT:Animal.Init entered
CHECK-NEXT:Dog.Init calling Fail
CHECK-NEXT:P is nil after failed Init
CHECK-NEXT:Dog.Init entered
CHECK-NEXT:Animal.Init entered
CHECK-NEXT:Dog.Init leaving normally
CHECK-NEXT:P is not nil after successful Init
CHECK-NEXT:Dog.Done running
CHECK-NEXT:Animal.Done running
*)
