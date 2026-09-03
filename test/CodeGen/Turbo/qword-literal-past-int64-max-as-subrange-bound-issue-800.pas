(*
Issue #800: the #799 fix for #795 taught SemaExpr.cpp and SemaStmt.cpp
(rejectOverflowLiteralUnlessQWordDest) to consult IntLitExpr::ExceedsInt64
before trusting a literal raw Value, but Sema::constBoundImpl (used to fold
subrange and array-index bounds) did not: it returned the literal plain
int64_t Value unchecked, which for a literal in (Int64::max, UInt64::max]
is that value two-complement bit pattern (18446744073709551615 reads back
as -1). A subrange bound is always folded into a plain signed int64_t
(Type::SubLo/SubHi), unlike an assignment or argument destination, which
can accept the same literal outright when the destination itself is a
64-bit unsigned QWord -- so there is no destination-shaped escape hatch
here, and the only honest outcome is a clear "integer literal ... is out of
range" diagnostic. Before the fix, this folded silently to -1 and 0, and
the bound-inversion check then reported the misleading "lower bound 0
exceeds upper bound -1, so the type has no values" instead.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: integer literal '18446744073709551615' is out of range
CHECK-NOT: exceeds upper bound
*)

program qwordLiteralAsSubrangeBound;
type
  TR = 0..18446744073709551615;
var
  x: TR;
begin
  x := 5;
  writeln(x);
end.
