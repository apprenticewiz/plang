(*
Issue #786's twin case for Turbo's ShortString dialect: the same
ExprIsShortStr FieldExpr branch in CGExprCore::emitExpr had the identical
ordering bug as the ExprIsVarStr one -- a qualified, no-parens call to a
parameterless method returning `string[N]` was taken for a genuine field of
that ShortString type and crashed codegen the same way.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program Min7Short;
type
  TBase = object
    function Name: string[30];
  end;
function TBase.Name: string[30]; begin Name := 'ShortBase'; end;
procedure ShowIt(var other: TBase);
begin
  writeln('Name=', other.Name);   { crashed here before the fix }
end;
var a: TBase;
begin
  ShowIt(a);
end.

(*
CHECK:Name=ShortBase
*)
