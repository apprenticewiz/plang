(*
Issue #773: a bare (no-parens) call to a parameterless FUNCTION method, used
in expression context, used to fail to resolve -- both from INSIDE another
method of the same object type ('A := Area;', Sema::checkIdent, which had no
fallback to the implicit-method lookup checkCallExpr already used for the
parenthesized 'Area()' spelling) and from OUTSIDE it, qualified but still
without parentheses ('S.Area', Sema::checkField, which had no equivalent
fallback to findObjectMethod either). Confirmed against a local `fpc -Mtp`
build of this identical program, which compiles clean with no parentheses
anywhere and prints 42.0 both times.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program BareFuncCall;
type
  TShape = object
    function Area: Real;
    procedure Report;
  end;
function TShape.Area: Real; begin Area := 42.0 end;
procedure TShape.Report;
var a: Real;
begin
  a := Area;              { bare, unqualified, inside another method }
  writeln(a:1:1)
end;
var s: TShape;
begin
  s.Report;
  writeln(s.Area:1:1)     { bare, qualified, from outside any method }
end.

(*
CHECK:42.0
CHECK-NEXT:42.0
*)
