(*
Turbo Tier 5, Cluster A item 1: two out-of-line bodies for the same
in-class heading is diagnosed (err_object_method_body_duplicate), not
silently accepting the second (or the first).
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program DuplicateBody;

type
  TAnimal = object
    procedure Speak;
  end;

procedure TAnimal.Speak;
begin
end;

procedure TAnimal.Speak;
begin
end;

begin
end.

(*
CHECK: error: method 'TAnimal.Speak' already has a defining declaration
*)
