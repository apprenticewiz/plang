(*
Turbo Tier 5, Cluster A item 7: visibility.  Confirmed against a local fpc
-Mtp build (priv1.pas/priv2.pas) that TP7's 'private' is scoped to the
whole declaring MODULE (program or unit), not to the exact object type --
a private field or method is reachable from a DESCENDANT type's own methods
(no 'protected' concept exists to distinguish that case) AND from ordinary
code with no method context at all, as long as it is all in the SAME
module.  A single-file Turbo program never has more than one module, so
every access in this test is necessarily "within its own declaring module"
-- this is exactly what the confirmed-real-scope means for the only shape
a -std=turbo program compiles as today (cross-unit object-type consumption
is Cluster B/item 8's own separate, not-yet-built work).  This test is
therefore a NO-FALSE-POSITIVE regression proof (private access that real
Borland/FPC accepts still compiles and runs here), not a proof that the
refusal diagnostic (err_object_private_field/err_object_private_method) can
ever fire yet -- see this item's own report for why.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program PrivateAccessWithinOneModule;

type
  TAnimal = object
    Name: string[20];
  private
    Secret: integer;
    procedure Hidden;
  public
    procedure Show;
  end;
  TDog = object(TAnimal)
    procedure Bark;
  end;

procedure TAnimal.Hidden;
begin
  writeln('hidden: ', Secret);
end;
procedure TAnimal.Show;
begin
  Secret := 2;
  Self.Hidden;
end;
procedure TDog.Bark;
begin
  Secret := 3;
  Self.Hidden;
end;

var
  D: TDog;
  A: TAnimal;
begin
  D.Show;
  D.Bark;
  { ordinary top-level code, no method context at all, still the same module }
  A.Secret := 42;
  writeln('external: ', A.Secret);
end.

(*
CHECK:hidden: 2
CHECK-NEXT:hidden: 3
CHECK-NEXT:external: 42
*)
