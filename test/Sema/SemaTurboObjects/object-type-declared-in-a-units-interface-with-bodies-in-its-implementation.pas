(*
Turbo Tier 5, Cluster A item 1: an object type declared in a unit's own
INTERFACE section, with its out-of-line method bodies given in the
IMPLEMENTATION section (a different scope), resolves and matches
correctly -- the interface's own Method symbols are re-registered into the
implementation's own scope by checkUnit (Sema.cpp) the same way an
ordinary interface procedure heading already is, and checkMethodBody's
plain Symtab.lookup by composite key finds them there.  (The Phase 7.6
sibling "never given a body" audit does NOT run across this particular
boundary -- see that audit's own comment, Sema.cpp -- for the identical
reason it already does not for an ordinary interface procedure; this test
only checks that a body IS found and matched, not that a missing one would
be caught here.)
*)

(*
RUN: %plang_ir -std=turbo -dump-vmt %s | FileCheck --strict-whitespace --match-full-lines %s
*)

unit AnimalUnit;

interface

type
  TAnimal = object
    Name: string;
    constructor Init(N: string);
    procedure Speak; virtual;
  end;

implementation

constructor TAnimal.Init(N: string);
begin
  Name := N;
end;

procedure TAnimal.Speak;
begin
end;

end.

(*
CHECK:(vmt TAnimal
CHECK-NEXT:  (fields (Name string[255] public))
CHECK-NEXT:  (methods (Init constructor) (Speak procedure virtual slot=0))
CHECK-NEXT:  (slots (0 Speak TAnimal)))
*)
