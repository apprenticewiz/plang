(*
Turbo Tier 5, Cluster A item 1: an out-of-line method body
('procedure T.M; ...') whose 'T.M' names no in-class heading declared in
object type T -- here, T exists but has no member named M at all -- is a
clean diagnostic (err_object_method_body_without_heading), not a silent
drop or a crash.
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program BodyWithoutHeading;

type
  TAnimal = object
    Name: string;
  end;

procedure TAnimal.Speak;
begin
end;

begin
end.

(*
CHECK: error: 'TAnimal.Speak' has no matching method heading in object type 'TAnimal'
*)
