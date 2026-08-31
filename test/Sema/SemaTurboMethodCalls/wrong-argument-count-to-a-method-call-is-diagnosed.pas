(*
Turbo Tier 5, Cluster A item 3: arity/argument-type checking for a method
call reuses Sema::checkCallArgs -- the SAME implementation an ordinary
procedure/function call gets -- via a synthetic SymbolKind::Proc stand-in
built from the resolved method's Symbol (Sema::checkMethodCall,
SemaExpr.cpp).  This is not a second, method-specific copy of arity
checking; err_wrong_arg_count is the one ordinary calls already use.
*)

(*
RUN: not %plang_ir -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program WrongArgumentCount;

type
  TAnimal = object
    procedure Speak(Times: Integer);
  end;

procedure TAnimal.Speak(Times: Integer);
begin
end;

var
  A: TAnimal;
begin
  A.Speak;
end.

(*
CHECK: error: 'TAnimal.Speak' expects 1 argument(s), got 0
*)
