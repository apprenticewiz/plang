(*
Turbo Tier 5, Cluster A item 3: calling an ANCESTOR's method on a
descendant-typed receiver -- Sema::checkMethodCall's ancestor-chain walk
(Symtab.lookup(objectMethodKey(Cur->Name, Method)) for Cur = TPuppy, then
TAnimal) has to find TAnimal.Done through TPuppy's own Parent link, the
same VMT-slot-table inheritance Cluster A item 1 already builds.
*)

(*
RUN: %plang_ir -std=turbo -dump-ast %s | FileCheck %s
*)

program InheritedMethodCall;

type
  TAnimal = object
    procedure Done;
  end;
  TPuppy = object(TAnimal)
    procedure Bark;
  end;

procedure TAnimal.Done;
begin
end;
procedure TPuppy.Bark;
begin
end;

var
  Pu: TPuppy;
begin
  Pu.Bark;
  Pu.Done;
end.

(*
CHECK: (methodcall Pu Bark)
CHECK-NEXT: (methodcall Pu Done)
*)
