(*
Issue #792: a non-virtual ancestor PLAIN method (never 'virtual' anywhere
in the chain) statically hidden by a same-named descendant method must
NOT warn, regardless of whether the signature matches -- there is no VMT
dispatch chain to break.  Real `fpc -Mtp` stays silent on this exact
construct.  Compare
non-virtual-redeclaration-hides-rather-than-overrides-and-warns.pas, whose
ancestor Speak IS virtual and so must keep warning.
*)

(*
RUN: %plang_ir -std=turbo -dump-vmt %s 2> %t.err | FileCheck --strict-whitespace --match-full-lines %s
RUN: FileCheck --check-prefix=NOWARN --allow-empty %s < %t.err
*)

program NonVirtualPlainMethodHideIsSilent;

type
  TAnimal = object
    procedure Speak;
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
NOWARN-NOT: hides the inherited

CHECK:(vmt TAnimal
CHECK-NEXT:  (fields)
CHECK-NEXT:  (methods (Speak procedure))
CHECK-NEXT:  (slots))
CHECK-NEXT:(vmt TDog (ancestor TAnimal)
CHECK-NEXT:  (fields)
CHECK-NEXT:  (methods (Speak procedure))
CHECK-NEXT:  (slots))
*)
