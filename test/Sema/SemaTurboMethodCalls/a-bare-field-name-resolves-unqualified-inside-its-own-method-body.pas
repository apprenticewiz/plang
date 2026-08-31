(*
Turbo Tier 5, Cluster A item 4: a method body may now name its own type's
field -- including one inherited from an ancestor -- WITHOUT 'Self.' at
all, both read and written; Sema::pushMethodSelfScope exposes every one of
Type::RecordFields (already the flattened ancestor-then-own list) as a
bare Var symbol for exactly the duration of the method body being checked.
Before this item, checkProcBody never even ran on a method body (Sema.cpp's
Phase 5b skipped every OwnerType-non-empty ProcDecl outright), so this
program would previously have compiled with the body simply unchecked, not
diagnosed one way or the other -- this positive case is what actually
proves the scope now exists and reaches an inherited field, not merely
that nothing rejects it.
*)

(*
RUN: %plang_ir -std=turbo -dump-ast %s | FileCheck %s
*)

program BareFieldInMethod;

type
  TAnimal = object
    Name: string[20];
  end;
  TDog = object(TAnimal)
    Breed: string[20];
    procedure Describe;
  end;

procedure TDog.Describe;
begin
  Name := 'Rex';
  Breed := Name;
end;

begin
end.

(*
CHECK: (assign Name "Rex")
CHECK-NEXT: (assign Breed Name)
*)
