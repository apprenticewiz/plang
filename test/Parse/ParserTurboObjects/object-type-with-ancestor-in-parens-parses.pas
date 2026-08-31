(*
Turbo Tier 5, Cluster A item 0: parsing only.  The optional ancestor clause
-- 'object(TAnimal)' -- names the ancestor object type as a plain string
(ObjectTypeNode::Ancestor), resolved later by Sema; see that field's own
comment (AstDecl.h) for why this follows the same not-yet-resolved-
cross-reference precedent as ImportClause::ModuleName.
*)

(*
RUN: %plang_ir -std=turbo -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program WithAncestor;

type
  TAnimal = object
    Name: string;
  end;
  TDog = object(TAnimal)
    Breed: string;
  end;

begin
end.

(*
CHECK:(program WithAncestor
CHECK-NEXT:  (typedef TAnimal (object (public (Name string))))
CHECK-NEXT:  (typedef TDog (object (ancestor TAnimal) (public (Breed string))))
CHECK-NEXT:  (compound))
*)
