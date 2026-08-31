(*
Turbo Tier 5, Cluster A item 6: Dispose(P, Done) -- confirmed against a
local `fpc -Mtp` build that the destructor's own output appears BEFORE the
memory is freed (dispose(p, Done) runs Done(), THEN the ordinary plain
deallocation new(p) alone already uses).  Done is declared 'virtual' here, so 'inherited Done' inside TDog.Done's own
body (statically calling TAnimal's own implementation, never redispatching
-- item 5's own contract) is what proves BOTH override bodies actually ran,
in the right order, from one Dispose(P, Done) call.
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
    constructor Init;
    destructor Done; virtual;
  end;
  PDog = ^TDog;
  TDog = object(TAnimal)
    destructor Done; virtual;
  end;

constructor TAnimal.Init;
begin
end;

destructor TAnimal.Done;
begin
  writeln('Animal.Done running');
end;

destructor TDog.Done;
begin
  writeln('Dog.Done running');
  inherited Done;
end;

var
  P: PDog;
begin
  New(P, Init);
  writeln('before dispose');
  Dispose(P, Done);
  writeln('after dispose');
end.

(*
CHECK:before dispose
CHECK-NEXT:Dog.Done running
CHECK-NEXT:Animal.Done running
CHECK-NEXT:after dispose
*)
