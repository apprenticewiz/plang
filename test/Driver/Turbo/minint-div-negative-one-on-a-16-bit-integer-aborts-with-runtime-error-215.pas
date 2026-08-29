(*
Turbo's Integer is a real, live 16-bit signed type (LangOptions::
defaultIntWidth() returns 16 under -std=turbo), so its own minint is
-32768, not ISO/EP's -9223372036854775808.  `MinInt16 div -1` has no
representable 16-bit result -- 32768 does not fit -- the same overflow
shape as the ISO/EP sibling test (division-overflow-reports.pas), just at
a narrower width.

RangeCheckGuards::emitDivOverflowCheck used to compare the dividend
against a hardcoded 64-bit minint no matter what width the division was
actually at: CGBinaryOps' Div arm widens both operands to i64 (ToI64) for
the SDiv itself, and MinInt16 sign-extended to i64 is nowhere near
INT64_MIN, so the guard never fired for this pair -- `a div b` silently
computed the wrapped, 64-bit-arithmetic answer (32768) and returned 0,
rather than reporting Borland/FPC's "Runtime error 215: Arithmetic
overflow error" (confirmed against `fpc -Mtp`) the way real Turbo/FPC
does.  CGBinaryOps now threads e.ResolvedType->Width (16 here) through to
the guard, the same pattern the sibling Shl/Shr case already used.

Like the sibling division-by-zero-aborts-with-exit-code-200 test, this
goes through the Turbo-only plang_tp_runerror(215) reporter -- exit
status 215 itself, not the shared ISO/EP plang_err_div_overflow path
(exit PlangRuntimeErrorStatus, 70, "plang runtime: ..." wording) -- and,
like emitDivZeroCheck/emitModDivisorCheck, this is unconditional: it
fires even without an explicit {$R+}, since it is the same
signed-overflow-UB guard the ISO/EP path's own
division-overflow-checked-even-without-range-checks.pas test exercises.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 215 %run %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 215 at $
CHECK-NOT: plang runtime:
*)

program minintdivnegone;
var
  a, b, n: Integer;
begin
  n := -32768;
  a := n;
  b := -1;
  writeln('unreachable: ', a div b);
end.
