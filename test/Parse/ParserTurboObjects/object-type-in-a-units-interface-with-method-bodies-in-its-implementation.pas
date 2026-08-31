(*
Turbo Tier 5, Cluster A item 0: parsing only.  A unit's implementation
section is exactly where a real Turbo Pascal program puts an interface-
declared object type's out-of-line method bodies.  parseUnitDeclarations
(ParseUnit.cpp) needed the same 'constructor'/'destructor' recognition as
the ordinary block-level and free-declaration-order paths (ParseDecl.cpp)
-- see the comment where that check was added -- since a unit's own
interface section can declare the object type and its implementation
section supply the bodies, exactly like this codebase's own unit tests
already do for ordinary procedures/functions (ParserTurboUnits).
*)

(*
RUN: %plang_ir -std=turbo -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
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
CHECK:(unit AnimalUnit
CHECK-NEXT:  (interface
CHECK-NEXT:    (typedef TAnimal (object (public (Name string)) (public constructor Init ((N string))) (public procedure Speak () virtual)))
CHECK-NEXT:    ())
CHECK-NEXT:  (implementation
CHECK-NEXT:    (constructor TAnimal.Init ((N string))
CHECK-NEXT:      (compound
CHECK-NEXT:        (assign Name N)))
CHECK-NEXT:    (procedure TAnimal.Speak ()
CHECK-NEXT:      (compound))
CHECK-NEXT:    ())
CHECK-NEXT:)
*)
