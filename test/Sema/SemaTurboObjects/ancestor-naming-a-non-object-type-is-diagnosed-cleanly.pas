(*
Turbo Tier 5, Cluster A item 1: an ancestor clause naming a real, declared
type that is not itself an object type is a clean diagnostic
(err_object_ancestor_not_object_type) -- confirmed against a local fpc -Mtp
build ("class type expected, but got ...").
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program AncestorNotObject;

type
  TInt = integer;
  TDog = object(TInt)
    Name: string;
  end;

begin
end.

(*
CHECK: error: ancestor 'TInt' is not an object type
*)
