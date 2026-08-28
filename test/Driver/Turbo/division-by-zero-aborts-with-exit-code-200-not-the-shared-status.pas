(*
Borland/FPC's "Runtime error 200: Division by zero" (confirmed against
`fpc -Mtp`) -- RangeCheckGuards::emitDivZeroCheck routes through the new
plang_tp_runerror(200) reporter under -std=turbo instead of the shared
ISO/EP plang_err_div_zero (exit PlangRuntimeErrorStatus, 70).  See the
sibling mod-by-zero-also-aborts-with-exit-code-200.pas for div's own
sibling operator: mod-by-zero reports the SAME number on real Turbo/FPC,
through emitModDivisorCheck's own (separate) zero check rather than this
one.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 200 %run %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 200 at $
CHECK-NOT: plang runtime:
*)

program divisionbyzero;
var
  a, b, c: Integer;
begin
  a := 10;
  b := 0;
  c := a div b;
  writeln('unreachable: ', c);
end.
