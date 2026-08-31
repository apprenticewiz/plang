(*
Turbo Tier 5, Cluster A item 1: a root object type (no ancestor) with no
virtual methods at all resolves cleanly and gets an empty VMT slot table --
the base case the whole slot-assignment algorithm builds on
(Sema::resolveObjectType, SemaType.cpp).  -dump-vmt is this item's own
debug-introspection mechanism (plang/Sema/DumpVmt.h): there is no CodeGen
for an object type yet (item 2's job), so this is what makes the resolved
Type::VmtSlots table independently checkable at all.
*)

(*
RUN: %plang_ir -std=turbo -dump-vmt %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program RootObject;

type
  TAnimal = object
    Name: string;
    constructor Init(N: string);
  end;

constructor TAnimal.Init(N: string);
begin
  Name := N;
end;

begin
end.

(*
CHECK:(vmt TAnimal
CHECK-NEXT:  (fields (Name string[255] public))
CHECK-NEXT:  (methods (Init constructor))
CHECK-NEXT:  (slots))
*)
