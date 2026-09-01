(*
FlowState::mergeWith (SemaFlow.cpp) must skip a dead side (one that left
through Exit/Halt/Break/Continue/RunError) when folding two branches
together at a join, rather than treating its own Assigned/ResultAssigned as
a live disagreement with the other side: the branch that Exits here DOES
set F before leaving, and the branch that falls through (Cond false) sets
it too, so every real path through this function assigns F and
warn_result_not_always_set must stay quiet.  Before mergeWith knew about a
dead side, this else-less if was handled by a hand-written shortcut that
only touched UndefAfterFor and happened to get this particular shape right
by luck (see the shortcut's own history); the explicit-else sibling of this
test (an-else-arm-that-exits-does-not-warn-either.pas) is the shape that
shortcut could not have covered.
*)

(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: does not assign to its result
*)

program p;
function F(Cond: Boolean): Integer;
begin
  if Cond then begin F := 1; Exit end;
  F := 2
end;
var r: Integer;
begin
  r := F(true)
end.
