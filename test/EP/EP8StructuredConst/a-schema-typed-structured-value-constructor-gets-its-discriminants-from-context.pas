(*
Issue #590: EP §6.8.7's structured value constructor ('TypeName[label:
value; ...]') used to reject every SCHEMA type outright ("type 'row' not
found for value constructor"), even though 'row' plainly IS a declared
type -- checkStructuredValue (SemaExpr.cpp) required the looked-up symbol
to be exactly SymbolKind::TypeAlias, but a schema type is registered as
SymbolKind::Schema.

A bare schema-name written as the constructor's own TypeName has no
discriminants of its own to instantiate with -- 'row[...]' has no syntax to
supply them the way 'new(p, 5)' or a variable declaration does -- so the
only source of a concrete instantiation is the assignment's own
destination type.  checkAssign now hands that in through
ExpectedValueType_ before checking a StructuredValueExpr RHS, the same
context a typed const's initializer already gets its type from.

RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0 99 0
CHECK-NEXT:3 4
*)

program p;
type
  row(n: integer)  = array[1..n] of integer;
  Pair(n: integer) = record x, y: integer end;
var
  v: row(5);
  pr: Pair(2);
begin
  v := row[2: 99; otherwise 0];
  writeln(v[1], ' ', v[2], ' ', v[5]);
  pr := Pair[x: 3; y: 4];
  writeln(pr.x, ' ', pr.y)
end.
