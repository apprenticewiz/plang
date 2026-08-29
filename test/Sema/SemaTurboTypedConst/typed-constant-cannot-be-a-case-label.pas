(*
The case-label sibling of typed-constant-cannot-be-an-array-bound.pas: a
typed constant is a SymbolKind::Var, not a SymbolKind::Const, so it is
refused as a case label the same way any other variable would be -- with no
extra rejection logic written for typed constants specifically.  See that
test's own comment for the full "why".

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: case label is not a constant
*)

const Five: Integer = 5;
var Y: Integer;
begin
  Y := 5;
  case Y of
    Five: writeln('five');
  else
    writeln('other');
  end;
end.
