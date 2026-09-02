(*
Issue #781: the qualified, outside-any-method counterpart of the companion
'...resolves-through-self-scope.pas' test in this directory. 'C.X', where X
is a parameterless FUNCTION method that shares its name with an inherited
FIELD, used to silently resolve to the inherited FIELD's stale value instead
of the METHOD. Sema::checkField's fieldByName lookup finds the field first
and, unlike Sema::checkCallExpr's #730 fix, had no retry of the
implicit-method fallback (findObjectMethod) when the found field is not
itself callable. Confirmed against a local `fpc -Mtp` build of this identical
program, which compiles clean and prints 100.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program FieldMethodBareCallOutside;
type
  TBase = object
    X: Integer;
    constructor Init;
  end;
  TChild = object(TBase)
    constructor Init;
    function X: Integer;
  end;
constructor TBase.Init; begin X := 42 end;
constructor TChild.Init; begin inherited Init end;
function TChild.X: Integer; begin X := 100 end;
var C: TChild;
begin
  C.Init;
  writeln(C.X);        { should call the method (100), not read the field (42) }
end.

(*
CHECK:100
*)
