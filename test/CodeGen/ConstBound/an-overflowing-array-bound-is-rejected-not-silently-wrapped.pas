(*
Issue #202: constBoundImpl's +, -, *, ** (isoPow), abs, sqr and unary minus
(SemaType.cpp) all did plain int64_t arithmetic with no overflow check -- UB
in the compiler itself (UBSan fires at the point of overflow) and, in a
release build, a silently wrapped value folded in as though it were the
constant the source actually named: `maxint + 1` used to fold to minint
with no diagnostic at all.  Since constBound's answer is what array bounds,
subrange bounds and case labels are built from, the wrong value did not stay
contained to one writeln -- it became the compiler's own belief about what
the program said a type's extent was.

This is the sharpest place that shows up.  Pre-fix, minint (from the wrapped
maxint + 1) as an array's upper bound read back as an INVERTED bound:
  error: lower bound 1 exceeds upper bound -9223372036854775808, so the
  type has no values
True of the wrapped number, but not an honest description of what is wrong
with the program -- the user wrote `maxint + 1`, not -9223372036854775808.
With the fold declining on overflow (Arith.h's checkedAdd, reached through
constBoundImpl the same way the runtime-shared div/mod guards already are),
the program is refused for the real reason: the bound is not a constant
expression, because the constant it names does not fit in an integer.
*)

(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: upper bound of array index type is not a constant expression
CHECK-NOT: exceeds upper bound
*)

program t;
var x: array[1..maxint+1] of integer;
begin end.
