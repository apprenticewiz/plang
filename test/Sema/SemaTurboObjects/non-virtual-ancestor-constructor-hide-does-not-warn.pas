(*
Issue #792: a non-virtual ancestor CONSTRUCTOR statically hidden by a
same-named, differently-signed descendant constructor (the standard
'Init'-at-every-level TP7 idiom, with 'virtual' never used anywhere in the
chain) must NOT warn -- there is no VMT dispatch chain for a non-virtual
declaration to break, and real `fpc -Mtp` stays silent on this exact
construct.  This regresses the false positive that
Sema::resolveObjectType used to fire for ANY inherited declaration of the
name, virtual or not; the fix narrows the check to only warn when the
nearest ancestor declaration (found via the same findMethodInChain call)
was itself 'virtual'.  Compare
non-virtual-redeclaration-hides-rather-than-overrides-and-warns.pas, whose
ancestor Speak IS virtual and so must keep warning.
*)

(*
RUN: %plang_ir -std=turbo -dump-vmt %s 2> %t.err | FileCheck --strict-whitespace --match-full-lines %s
RUN: FileCheck --check-prefix=NOWARN --allow-empty %s < %t.err
*)

program NonVirtualConstructorHideIsSilent;

type
  TAnimal = object
    constructor Init(AName: string);
  end;
  TDog = object(TAnimal)
    constructor Init(AName: string; ABreed: string);
  end;

constructor TAnimal.Init(AName: string);
begin
end;

constructor TDog.Init(AName: string; ABreed: string);
begin
end;

begin
end.

(*
NOWARN-NOT: hides the inherited

CHECK:(vmt TAnimal
CHECK-NEXT:  (fields)
CHECK-NEXT:  (methods (Init constructor))
CHECK-NEXT:  (slots))
CHECK-NEXT:(vmt TDog (ancestor TAnimal)
CHECK-NEXT:  (fields)
CHECK-NEXT:  (methods (Init constructor))
CHECK-NEXT:  (slots))
*)
