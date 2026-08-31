(*
Turbo Tier 5, Cluster A item 2: `_vptr` does not have to be introduced by
the ROOT of a hierarchy -- here TAnimal (the root) declares no virtual
method at all, TDog is the first to declare one, and TPuppy is a further
descendant of THAT.  Type::introducesVptr() singles out TDog specifically
(its own VmtSlots is non-empty but its Parent TAnimal's is empty), and
CGTypes::layoutOfObject places `_vptr` at the level that answers true --
TDog, not TAnimal and not TPuppy -- with TPuppy inheriting it for free at
the identical offset TDog itself gave it, through the nested embedding
(TDog's own struct sits as an untouched prefix of TPuppy's storage).

Confirmed against a local `fpc -Mtp` build: TAnimal (Name: array[1..10] of
char, no virtual method) is exactly 10 bytes -- no vptr overhead, since it
never introduces one.  TDog adds Breed (a 4-byte LongInt, offset 12 after
Name's 10 bytes rounded up to LongInt's own 4-byte alignment) and its own
first virtual method: 12 + 4 = 16, rounded up to the vptr's own 8-byte
alignment (still 16) plus the vptr itself = 24.  TPuppy adds LitterSize
starting at offset 24 (TDog's own full, vptr-inclusive size): 24 + 4 = 28,
rounded up to 32.
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
  end;
  TDog = object(TAnimal)
    Breed: LongInt;
    procedure Speak; virtual;
  end;
  TPuppy = object(TDog)
    LitterSize: LongInt;
  end;
var
  a: TAnimal;
  d: TDog;
  pu: TPuppy;
procedure TDog.Speak;
begin
end;
begin
  writeln(SizeOf(a), ' ', SizeOf(d), ' ', SizeOf(pu));
end.

(*
CHECK:10 24 32
*)
