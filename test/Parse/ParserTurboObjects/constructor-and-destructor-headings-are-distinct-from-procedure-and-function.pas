(*
Turbo Tier 5, Cluster A item 0: parsing only.  'constructor' and
'destructor' are already lexed as their own reserved words under -std=turbo
(TokenKinds.def), genuinely distinct from 'procedure'/'function' rather
than an attribute layered onto an ordinary routine -- ProcDecl gets its own
IsConstructor/IsDestructor flags (the same flat-bool idiom as the existing
IsFunction/IsForward) instead of overloading IsFunction.  This checks both
the in-class heading and the out-of-line dotted body for each.
*)

(*
RUN: %plang_ir -std=turbo -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program CtorDtor;

type
  TAnimal = object
    constructor Init(N: string);
    destructor Done; virtual;
  end;

constructor TAnimal.Init(N: string);
begin
end;

destructor TAnimal.Done;
begin
end;

begin
end.

(*
CHECK:(program CtorDtor
CHECK-NEXT:  (typedef TAnimal (object (public constructor Init ((N string))) (public destructor Done () virtual)))
CHECK-NEXT:  (constructor TAnimal.Init ((N string))
CHECK-NEXT:    (compound))
CHECK-NEXT:  (destructor TAnimal.Done ()
CHECK-NEXT:    (compound))
CHECK-NEXT:  (compound))
*)
