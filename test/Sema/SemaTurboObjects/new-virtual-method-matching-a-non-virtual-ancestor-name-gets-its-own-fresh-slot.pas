(*
Turbo Tier 5, Cluster A item 1: confirmed against a local fpc -Mtp build
that a NEW virtual method whose name only coincidentally matches a
NON-virtual ancestor method (which therefore has no VMT slot of its own to
find -- the search in Sema::resolveObjectType only ever looks inside
VmtSlots, which holds virtual methods alone) compiles clean, with TWO
independent identities: TAnimal's own static, non-virtual X, and TDog's
own virtual, VMT-dispatched X, which gets a brand-new slot rather than
being treated as an override of anything (there is nothing virtual to
override) or refused as a collision (methods may freely reuse an inherited
name; see descendant-method-reusing-an-inherited-field-name-is-legal.pas
for the field/method version of that same rule).
*)

(*
RUN: %plang_ir -std=turbo -dump-vmt %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program FreshSlotOverNonVirtualAncestor;

type
  TAnimal = object
    procedure X;
  end;
  TDog = object(TAnimal)
    procedure X; virtual;
  end;

procedure TAnimal.X;
begin
end;

procedure TDog.X;
begin
end;

begin
end.

(*
CHECK:(vmt TAnimal
CHECK-NEXT:  (fields)
CHECK-NEXT:  (methods (X procedure))
CHECK-NEXT:  (slots))
CHECK-NEXT:(vmt TDog (ancestor TAnimal)
CHECK-NEXT:  (fields)
CHECK-NEXT:  (methods (X procedure virtual slot=0))
CHECK-NEXT:  (slots (0 X TDog)))
*)
