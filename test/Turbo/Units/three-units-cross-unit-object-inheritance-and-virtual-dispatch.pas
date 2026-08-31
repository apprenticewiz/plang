(*
Turbo Tier 5, Cluster B item 8 (object types crossing a unit boundary) --
the tier's own capstone-scale proof, mirroring three-units-separate-
compilation-real-string-pipeline.pas's "three real units, each compiled
entirely on its own, sources DELETED before the final program compile"
pattern (Tier 4's own established strongest-possible-proof shape for
separate compilation), extended here to an object-type ancestor chain that
spans all three:

  - Unit A declares the ROOT object type TAnimal, with one PUBLIC field
    (Name), one PRIVATE field (Legs -- exercising the requirement that an
    ancestor's private fields still have to be serialized into its own
    .tui, transitively, so a DESCENDANT declared in a different unit gets
    correct field offsets for its own fields even though Legs itself stays
    inaccessible outside UnitA), and one virtual method (Speak) with a real
    Pascal body.
  - Unit B 'uses' unit A and declares TDog = object(TAnimal), OVERRIDING
    Speak (which itself calls 'inherited Speak', reaching back across the
    unit boundary to TAnimal's own original implementation) and adding a
    field of its own (Breed) AFTER the inherited ones -- Breed's own byte
    offset only comes out right if TAnimal's real size (including the
    PRIVATE Legs field declared in a unit B cannot see by name) was counted.
  - Program C 'uses' BOTH unit A and unit B directly -- confirmed against a
    real `fpc -Mtp` build that a 'uses' clause does NOT transitively
    re-export a name it itself imported (program C could not otherwise name
    TAnimal at all: FPC rejects it with "Identifier not found") -- creates a
    TDog, and reaches it through a TAnimal-TYPED POINTER, proving virtual
    dispatch through an ancestor-typed pointer still finds the DESCENDANT's
    own override, compiled in a wholly separate translation unit, not
    TAnimal's own original implementation.

Both unit .pas sources are deleted before program C is ever compiled, so
this only passes if unit A and unit B's own object files and .tui interface
files alone are enough to check and link program C -- genuine separate
compilation, not two units and a program still secretly sharing one
compiland's AST/Sema state.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/unita.pas -o %t.dir/unita.o
RUN: rm %t.dir/unita.pas
RUN: %plang -std=turbo -I%t.dir -c %t.dir/unitb.pas -o %t.dir/unitb.o
RUN: rm %t.dir/unitb.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/progc.pas %t.dir/unita.o %t.dir/unitb.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:Rex the Labrador barks instead of making a generic sound
CHECK-NEXT:Rex makes a generic animal sound (4 legs)
CHECK-NEXT:Rex: Woof!
CHECK-NEXT:Legs via ancestor accessor: 4
*)

//--- unita.pas
unit UnitA;

interface

type
  TAnimal = object
    Name: string;
  private
    Legs: Integer;
  public
    procedure Speak; virtual;
    procedure SetLegs(N: Integer);
    function GetLegs: Integer;
  end;

implementation

procedure TAnimal.Speak;
begin
  writeln(Name, ' makes a generic animal sound (', Legs, ' legs)');
end;

procedure TAnimal.SetLegs(N: Integer);
begin
  Legs := N;
end;

function TAnimal.GetLegs: Integer;
begin
  GetLegs := Legs;
end;

end.

//--- unitb.pas
unit UnitB;

interface

uses UnitA;

type
  TDog = object(TAnimal)
    Breed: string;
    procedure Speak; virtual;
    procedure Bark;
  end;

implementation

procedure TDog.Speak;
begin
  writeln(Name, ' the ', Breed, ' barks instead of making a generic sound');
  inherited Speak;
end;

procedure TDog.Bark;
begin
  writeln(Name, ': Woof!');
end;

end.

//--- progc.pas
program ProgC;

uses UnitA, UnitB;

var
  D: TDog;
  P: ^TAnimal;

begin
  D.Name := 'Rex';
  D.SetLegs(4);
  D.Breed := 'Labrador';

  P := @D;
  P^.Speak;      { virtual dispatch through an ANCESTOR-typed pointer }

  D.Bark;        { TDog's own, non-virtual method }
  writeln('Legs via ancestor accessor: ', D.GetLegs());
end.
