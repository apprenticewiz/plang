(*
Issue #201: `(-maxint-1) div (-1)` (and identically, `mod`) in a CONSTANT
expression made Sema's constBoundImpl (SemaType.cpp) evaluate int64 L/R
directly for L = INT64_MIN, R = -1 -- the one nonzero-divisor pair with no
representable quotient (+2^63 does not fit a positive int64_t).  Both C's
`/` and `%` are signed-overflow UB for it, and on this hardware that is a
hardware SIGFPE trap (x86 idiv faults on overflow the same way it faults on
a zero divisor), so the COMPILER ITSELF crashed folding the constant, before
its own diagnostic machinery ever ran.  lib/CodeGen/ConstFold.cpp's
tryEvalConstInt/evalConst had the identical unguarded L/R -- reachable from
CodeGen's own constant lowering (constantValueOf) whenever Sema declines to
fold, which is exactly what happens once Sema is fixed -- so both had to
learn the same guard, or the crash would just move from Sema to CodeGen
instead of going away.

The runtime path for the identical operation (a division that happens to
compute minint / -1 on values only known at run time) already traps
cleanly -- see RangeCheckGuards::emitDivOverflowCheck, fixed in 0.3.1, and
division-overflow-reports.pas next to this file.  The fix is for the
constant folders to refuse to fold this one pair (Arith.h's divOverflows,
alongside the divide-by-zero check already there) instead of evaluating it,
the same way they already refuse to fold a divide by zero.  Declining
leaves the constant's value to be computed the way any other general
constant expression Sema cannot fold already is (emitRuntimeConst), so it
reaches the very runtime guard above instead of a second, compiler-side
crash: this program now compiles cleanly and traps at run time instead.
*)

(*
RUN: %plang %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: no representable result
*)

program t;
const d = (-maxint-1) div (-1);
begin writeln(d) end.
