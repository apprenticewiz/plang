(*
Turbo Tier 5, Cluster A item 3: 'Obj.NoSuchMethod(...)' where NoSuchMethod
names nothing in TAnimal's own ancestor chain -- err_object_method_not_found,
the object-typed-receiver sibling of err_no_such_field.
*)

(*
RUN: not %plang_ir -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program UndeclaredMethod;

type
  TAnimal = object
    procedure Speak;
  end;

procedure TAnimal.Speak;
begin
end;

var
  A: TAnimal;
begin
  A.Meow;
end.

(*
CHECK: error: object type 'TAnimal' has no method 'Meow'
*)
