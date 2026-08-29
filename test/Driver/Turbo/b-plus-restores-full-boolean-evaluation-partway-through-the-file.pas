(*
The load-bearing proof that BoolEval feeds a real, position-keyed
SwitchTable (Basic/SwitchTable.h) rather than merely parsing {$B+}/{$B-}
and doing nothing with it -- same shape as the sibling {$R+} test
(switch-directive-r-plus-turns-range-checks-on-partway-through-the-file.pas):
a side-effecting right operand does NOT run before {$B+}, in the same
`false and X` shape the short-circuit tests above use, and DOES run once
{$B+} takes effect at that point in the source, even though X's value
still cannot change the already-decided result (false and anything is
false).  If it did not, {$B+} would only be recognized syntax, not a
directive a codegen query at a later source location actually honors.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK-NOT: SIDEEFFECT CALLED
CHECK: before {$B+}: b=FALSE
CHECK-NEXT: SIDEEFFECT CALLED
CHECK-NEXT: after {$B+}: b=FALSE
*)

program bplus_restores_full_eval;

function SideEffect: Boolean;
begin
  writeln('SIDEEFFECT CALLED');
  SideEffect := true
end;

var b: Boolean;
begin
  b := false and SideEffect;
  writeln('before {$B+}: b=', b);
  {$B+}
  b := false and SideEffect;
  writeln('after {$B+}: b=', b)
end.
