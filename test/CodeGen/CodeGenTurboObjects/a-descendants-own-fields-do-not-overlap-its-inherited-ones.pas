(*
Turbo Tier 5, Cluster A item 2: a three-level inheritance chain
(TAnimal -> TDog -> TPuppy) round-trips through SizeOf at every level,
proving each level's own fields plus its ancestor's are accounted for
exactly once, with no overlap and no field silently dropped.

Real Borland/FPC object layout is RECURSIVE, not a single flat struct: a
descendant's storage is its immediate ancestor's own already-laid-out,
already-tail-padded storage sitting as an untouched prefix, with the
descendant's own new fields placed strictly after that WHOLE prefix, not
merely after however many raw bytes the ancestor's fields occupied before
its own trailing alignment padding.  Confirmed against a local `fpc -Mtp`
build: TAnimal (Name: array[1..10] of char, then a virtual method) is 24
bytes (10 bytes Name, padded to 16 for the vptr's own 8-byte alignment,
plus the vptr itself); TDog adds Breed (a 4-byte LongInt) STARTING AT
OFFSET 24 (TAnimal's own full, vptr-inclusive size), giving 28, rounded up
to TDog's own 8-byte alignment: 32; TPuppy adds LitterSize (also 4 bytes)
STARTING AT OFFSET 32 (TDog's own full size, not merely "after Breed's raw
4 bytes" which would have been 28), giving 36, rounded up to 40.  A field
access ('.') on an object type is not this item's job (a later cluster
item's), so this stays index-blind and checks only SizeOf -- but SizeOf
disagreeing between Sema::byteSizeOf and CGTypes::layoutOfObject is a hard
codegenICE crash (checkSizeAgreement/checkObjectFieldOffsetAgreement,
CGTypes.cpp), so a clean run here already proves the two independent
implementations of the layout agree, not merely that the totals below
happen to print right.
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
    Name: array[1..10] of char;
    procedure Speak; virtual;
  end;
  TDog = object(TAnimal)
    Breed: LongInt;
  end;
  TPuppy = object(TDog)
    LitterSize: LongInt;
  end;
var
  a: TAnimal;
  d: TDog;
  pu: TPuppy;
procedure TAnimal.Speak;
begin
end;
begin
  writeln(SizeOf(a), ' ', SizeOf(d), ' ', SizeOf(pu));
end.

(*
CHECK:24 32 40
*)
