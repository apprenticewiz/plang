(*
Issue #786's fix (routing an implicit-method-call FieldExpr to
emitImplicitMethodFieldCall before the ExprIsVarStr/ExprIsShortStr FieldExpr
branches, in both CGExprCore::emitExpr and CGExprCore::emitLValue) must not
disturb the ordinary case: a genuine `string` FIELD (IsImplicitMethodCall ==
false, findObjectMethod found no same-named method) still resolves through
FieldAccess.emitFieldGEP exactly as before, both read and written, and both
bare-inside-a-method and qualified-from-outside -- alongside a same-object,
non-clashing string-returning METHOD accessed the same two ways.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program FieldVsMethod;
type
  TThing = object
    Label_: string;
    function Greeting: string;
    procedure SetLabel(const s: string);
  end;
function TThing.Greeting: string; begin Greeting := 'Hello' end;
procedure TThing.SetLabel(const s: string);
begin
  Label_ := s;         { bare field WRITE, no clash }
end;
var T: TThing;
begin
  T.SetLabel('World');
  writeln(T.Label_);     { qualified plain string field read }
  writeln(T.Greeting);   { qualified implicit method call, same object }
  T.Label_ := 'Again';   { qualified plain string field WRITE }
  writeln(T.Label_);
end.

(*
CHECK:World
CHECK-NEXT:Hello
CHECK-NEXT:Again
*)
