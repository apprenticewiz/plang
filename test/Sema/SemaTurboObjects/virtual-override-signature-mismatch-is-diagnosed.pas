(*
Turbo Tier 5, Cluster A item 1: confirmed against a local fpc -Mtp build
("function header doesn't match the previous declaration") that a virtual
method overriding an inherited one of the same name must match its
signature exactly -- unlike a non-virtual redeclaration of the same name
(a static hide, free to differ in signature; see
non-virtual-redeclaration-hides-rather-than-overrides-and-warns.pas).
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program VirtualOverrideMismatch;

type
  TAnimal = object
    procedure Speak(X: integer); virtual;
  end;
  TDog = object(TAnimal)
    procedure Speak(X: string); virtual;
  end;

procedure TAnimal.Speak(X: integer);
begin
end;

procedure TDog.Speak(X: string);
begin
end;

begin
end.

(*
CHECK: error: virtual method 'Speak' does not match the signature of the inherited method it overrides
*)
