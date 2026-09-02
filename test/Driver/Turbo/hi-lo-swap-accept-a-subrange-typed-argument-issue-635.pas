(*
Issue #635: Hi/Lo/Swap rejected every subrange-typed argument, regardless
of width -- checkCallExpr's own guard (SemaExpr.cpp) tested ArgTy->Kind ==
TypeKind::Integer directly, and a subrange is a distinct TypeKind from
plain Integer in this codebase's type system even though it is ordinally
an integer (ISO §6.4.2.4: a subrange takes its values from its host
type). `ordinalBase` -- unwrapping a subrange to its host type -- is what
the guard checks now, matching `fpc -Mtp`'s own acceptance of a
subrange-typed argument here (`13330` for the Word-range case below, the
same answer the sibling hi-lo-swap-at-every-width test's plain-Word case
gets).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:18 52 13330
*)

program hi_lo_swap_subrange;
var
  w: 0..65535;
begin
  w := $1234;
  writeln(Hi(w), ' ', Lo(w), ' ', Swap(w));
end.
