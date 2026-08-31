(*
Turbo Tier 5, Cluster A item 1: confirmed against a local fpc -Mtp build
("Duplicate identifier" / "Procedure overloading is switched off") that a
field and a method sharing a name within ONE object type is refused --
real Turbo Pascal objects have no method overloading, and fields/methods
share one namespace per declaration level.
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program DuplicateMember;

type
  TFoo = object
    X: integer;
    procedure X;
  end;

procedure TFoo.X;
begin
end;

begin
end.

(*
CHECK: error: duplicate member name 'X' in object type 'TFoo'
*)
