(*
Issue #781: a bare (no-parens) reference to a parameterless FUNCTION method
that shares a name with an inherited FIELD, from INSIDE another method of the
same object type ('v := X;'), used to silently resolve to the inherited
FIELD's stale value instead of the METHOD. Sema::checkIdent's plain
Symtab.lookup finds the field first (pushMethodSelfScope registers every
field, own and inherited, as an ordinary Var symbol marked IsSelfScopeField),
and unlike Sema::checkCallExpr -- specifically patched for issue #730 to
retry the implicit-method lookup when the found symbol is a non-callable
self-scope field -- checkIdent had no equivalent retry, so it returned the
field's declared type without ever trying checkImplicitMethodIdent (#773).
Confirmed against a local `fpc -Mtp` build of this identical program, which
compiles clean and prints 100.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program FieldMethodBareCallInside;
type
  TBase = object
    X: Integer;
    constructor Init;
  end;
  TChild = object(TBase)
    constructor Init;
    function X: Integer;
    procedure CallIt;
  end;
constructor TBase.Init; begin X := 42 end;
constructor TChild.Init; begin inherited Init end;
function TChild.X: Integer; begin X := 100 end;
procedure TChild.CallIt;
var v: Integer;
begin
  v := X;              { should call the method (100), not read the field (42) }
  writeln(v);
end;
var C: TChild;
begin
  C.Init;
  C.CallIt;
end.

(*
CHECK:100
*)
