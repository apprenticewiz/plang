(*
Turbo Tier 5, Cluster A item 1: an ancestor clause naming a type that was
never declared at all is a clean diagnostic (err_object_ancestor_not_found),
not a crash or a cascade -- confirmed against a local fpc -Mtp build
("Identifier not found").
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program MissingAncestor;

type
  TDog = object(TNoSuchAnimal)
    Name: string;
  end;

begin
end.

(*
CHECK: error: ancestor type 'TNoSuchAnimal' is not declared
*)
