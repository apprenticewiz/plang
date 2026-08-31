(*
Turbo Tier 5, Cluster A item 1: an out-of-line body's own heading
(parameters, return type) must match the in-class heading it is the body
for -- the same arity/parameter-type check as an ordinary forward
declaration's own body, just worded for a method
(err_object_method_param_count/_param_type instead of
err_forward_param_count/_param_type).
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program BodySignatureMismatch;

type
  TAnimal = object
    procedure Speak(X: integer);
  end;

procedure TAnimal.Speak(X: string);
begin
end;

begin
end.

(*
CHECK: error: parameter 'X' of 'TAnimal.Speak': type 'string[255]' does not match the heading's type 'integer'
*)
