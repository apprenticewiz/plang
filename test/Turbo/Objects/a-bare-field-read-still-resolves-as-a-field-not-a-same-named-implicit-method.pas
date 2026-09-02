(*
Issue #773 companion: the fix that lets a bare identifier/qualified field
access fall back to a parameterless FUNCTION method (checkIdent's own
checkImplicitMethodIdent, checkField's own findObjectMethod fallback) is
tried ONLY once an ordinary field lookup has already failed -- see both
functions' own comments (SemaExpr.cpp). A genuine field must keep reading
(and writing) as a plain field, both from inside a method of the same
object type and from outside it, with no interference from a completely
unrelated sibling FUNCTION method of a different name. Confirmed against a
local `fpc -Mtp` build of this identical program.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program BareFieldStillPlainField;
type
  TShape = object
    Width: Real;
    function Area: Real;
    procedure Grow;
  end;
function TShape.Area: Real;
begin
  Area := Width * 2.0;
end;
procedure TShape.Grow;
begin
  Width := Width + 1.0;
end;
var s: TShape;
begin
  s.Width := 5.0;
  s.Grow;
  writeln(s.Width:1:1);
  writeln(s.Area:1:1);
end.

(*
CHECK:6.0
CHECK-NEXT:12.0
*)
