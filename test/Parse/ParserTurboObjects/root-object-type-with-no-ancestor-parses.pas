(*
Turbo Tier 5, Cluster A item 0: parsing only.  A "root" object type -- no
'(Ancestor)' clause at all -- is legal real Turbo Pascal (confirmed against
a local fpc -Mtp build); it just starts its own layout from scratch, which
is Sema's problem for a later item.  This is the simplest possible
ObjectTypeNode: one field, no ancestor, no methods, no visibility sections.
*)

(*
RUN: %plang_ir -std=turbo -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program RootObject;

type
  TAnimal = object
    Name: string;
  end;

begin
end.

(*
CHECK:(program RootObject
CHECK-NEXT:  (typedef TAnimal (object (public (Name string))))
CHECK-NEXT:  (compound))
*)
