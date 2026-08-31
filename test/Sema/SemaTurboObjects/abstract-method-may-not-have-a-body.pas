(*
Turbo Tier 5, Cluster A item 1: confirmed against a local fpc -Mtp build
("Abstract methods shouldn't have any definition") that an abstract method
may never be given a body, out-of-line or otherwise.
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program AbstractHasBody;

type
  TShape = object
    procedure Draw; virtual; abstract;
  end;

procedure TShape.Draw;
begin
end;

begin
end.

(*
CHECK: error: abstract method 'TShape.Draw' may not have a body
*)
