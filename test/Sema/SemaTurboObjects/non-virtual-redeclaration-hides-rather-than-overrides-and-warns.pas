(*
Turbo Tier 5, Cluster A item 1: confirmed against a local fpc -Mtp build
("An inherited method is hidden by ...") that redeclaring an inherited
method's name WITHOUT 'virtual' compiles cleanly -- it statically HIDES the
ancestor's method rather than overriding it through the VMT.  TDog's own
slot table is untouched: slot 0 still names TAnimal as Speak's own
implementing type, and TDog's own Speak (no slot at all, VmtSlot stays -1
-- printed with no 'slot=' at all) is a completely separate, statically-
resolved identity.  This is a warning, not an error (warn_object_method_
hides_inherited), so this test does not use 'not'.
*)

(*
RUN: %plang_ir -std=turbo -dump-vmt %s 2> %t.err | FileCheck --strict-whitespace --match-full-lines %s
RUN: FileCheck --check-prefix=WARN %s < %t.err
*)

program HideNotOverride;

type
  TAnimal = object
    procedure Speak; virtual;
  end;
  TDog = object(TAnimal)
    procedure Speak;
  end;

procedure TAnimal.Speak;
begin
end;

procedure TDog.Speak;
begin
end;

begin
end.

(*
WARN: warning: method 'Speak' hides the inherited method of the same name; declare 'virtual' to override it instead

CHECK:(vmt TAnimal
CHECK-NEXT:  (fields)
CHECK-NEXT:  (methods (Speak procedure virtual slot=0))
CHECK-NEXT:  (slots (0 Speak TAnimal)))
CHECK-NEXT:(vmt TDog (ancestor TAnimal)
CHECK-NEXT:  (fields)
CHECK-NEXT:  (methods (Speak procedure))
CHECK-NEXT:  (slots (0 Speak TAnimal)))
*)
