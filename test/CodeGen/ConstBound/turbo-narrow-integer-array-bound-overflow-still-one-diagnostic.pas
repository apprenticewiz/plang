(*
Sibling of turbo-integer-const-arithmetic-overflow-is-rejected.pas: the
same width-generic threading fixes constBoundImpl for every caller of
constBound, not only a plain `const` declaration.  An array bound already
had its own "not a constant expression" diagnostic for a declined fold
(an-overflowing-array-bound-is-rejected-not-silently-wrapped.pas is the
identical shape for a 64-bit int64 overflow, which every dialect shares);
this proves the SAME diagnostic -- and only that one, not a second --
fires here too once the narrow Turbo Integer overflow is what declines
the fold, so a Turbo program never builds an array from a silently-
wrapped bound the way `30000 + 30000` used to.

RUN: not %plang_ir -std=turbo -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: upper bound of array index type is not a constant expression
CHECK-NOT: constant expression is out of range
*)

program t;
var x: array[1..30000+30000] of Integer;
begin end.
