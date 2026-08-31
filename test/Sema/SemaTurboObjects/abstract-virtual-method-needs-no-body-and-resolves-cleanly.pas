(*
Turbo Tier 5, Cluster A item 1 supersedes item 0's own placeholder
(err_object_type_not_yet_supported, since removed -- issue #308 part (b))
with real resolution: an object type whose only method is
virtual-and-abstract needs no out-of-line body at all
(err_object_method_never_defined does not apply -- see that diagnostic's
own comment) and now resolves cleanly, VMT slot included.  This is the
positive twin of abstract-method-may-not-have-a-body.pas (which checks the
opposite mistake -- an abstract method IS given a body).
*)

(*
RUN: %plang_ir -std=turbo -dump-vmt %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program AbstractVirtualNeedsNoBody;

type
  TShape = object
    procedure Draw; virtual; abstract;
  end;

begin
end.

(*
CHECK:(vmt TShape
CHECK-NEXT:  (fields)
CHECK-NEXT:  (methods (Draw procedure virtual abstract slot=0))
CHECK-NEXT:  (slots (0 Draw TShape)))
*)
