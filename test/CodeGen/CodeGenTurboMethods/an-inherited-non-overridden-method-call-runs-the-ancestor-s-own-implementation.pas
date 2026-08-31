(*
Turbo Tier 5, Cluster A item 4: 'D.GetName' for a TDog D calls a method
GetName that ONLY TAnimal (its ancestor) declares -- Sema resolved this to
TAnimal's own implementation (SemaExpr.cpp's checkMethodCall ancestor-chain
walk, item 3), so CodeGen must mangle and call TAnimal's own symbol
(pas_tanimal$getname), not synthesize one from the RECEIVER's declared type
(which would be pas_tdog$getname -- a symbol nothing ever defines, an
unresolved-external-symbol link failure at best, or -- worse -- silently
matching an unrelated same-named method some OTHER descendant happens to
define).  D's own storage still starts with TAnimal's own sub-object as an
untouched prefix (CGTypes::layoutOfObject's nested layout), so calling
TAnimal's implementation with D's own address as Self reads and writes the
right bytes even though the method's OWN code was compiled against
TAnimal's narrower type.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  TAnimal = object
    Name: string[20];
    procedure SetName(N: string);
    function GetName: string;
  end;
  TDog = object(TAnimal)
    Breed: string[20];
    procedure SetBreed(B: string);
    function Describe: string;
  end;

procedure TAnimal.SetName(N: string);
begin
  Name := N;
end;

function TAnimal.GetName: string;
begin
  GetName := Name;
end;

procedure TDog.SetBreed(B: string);
begin
  Breed := B;
end;

function TDog.Describe: string;
begin
  Describe := Self.GetName() + ' the ' + Breed;
end;

var
  d: TDog;
begin
  d.SetName('Rex');
  d.SetBreed('Collie');
  writeln(d.GetName());
  writeln(d.Describe());
end.

(*
CHECK:Rex
CHECK-NEXT:Rex the Collie
*)
