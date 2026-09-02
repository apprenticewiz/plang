(*
Issue #781's fix (Sema::checkIdent/checkField now retry the implicit-method
fallback when a found symbol is a non-callable self-scope field / non-
callable object field) must not disturb the common, non-clashing case: a
plain field with no same-named method sibling anywhere in the object's
ancestor chain has findObjectMethod/checkImplicitMethodIdent return their
"no such method" sentinel, so the field's own value is read exactly as
before -- both bare-inside-a-method and qualified-from-outside, and both
before and after a plain assignment to the field. Also re-confirms a bare
call to a non-shadowed method (#773) and a qualified one still work
alongside a same-type, non-clashing field, all in the one object type.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program PlainFieldNoClash;
type
  TThing = object
    Y: Integer;
    function Area: Integer;
    procedure Go;
  end;
function TThing.Area: Integer; begin Area := Y * 2 end;
procedure TThing.Go;
var a: Integer;
begin
  Y := 7;
  a := Area;     { bare method call, no clash }
  writeln(a);
  writeln(Y);    { plain bare field read, no clash }
end;
var T: TThing;
begin
  T.Go;
  T.Y := 21;
  writeln(T.Y);       { qualified plain field read, no clash }
  writeln(T.Area);    { qualified bare method call, no clash }
end.

(*
CHECK:14
CHECK-NEXT:7
CHECK-NEXT:21
CHECK-NEXT:42
*)
