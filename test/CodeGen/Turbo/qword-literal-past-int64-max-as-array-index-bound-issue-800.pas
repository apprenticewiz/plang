(*
Issue #800: the same gap as the sibling
qword-literal-past-int64-max-as-subrange-bound-issue-800.pas test, but for
an array index type instead of a standalone subrange declaration --
Sema::foldBounds folds both through the same Sema::constBoundImpl, and both
resolveTypeImpl arms (named subrange, array index) call it. A literal past
Int64::max used as an array upper index bound must give the same clear
"integer literal ... is out of range" diagnostic, not the misleading
bound-inversion message its raw two-complement bit pattern used to
produce.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: integer literal '18446744073709551615' is out of range
CHECK-NOT: exceeds upper bound
*)

program qwordLiteralAsArrayIndexBound;
var
  a: array[0..18446744073709551615] of Byte;
begin
  a[0] := 1;
  writeln(a[0]);
end.
