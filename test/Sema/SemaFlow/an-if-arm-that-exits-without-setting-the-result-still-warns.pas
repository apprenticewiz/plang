(*
mergeWith's dead-side rule (see an-if-arm-that-sets-the-result-then-exits-
does-not-warn.pas) means a dead branch is dropped from every later join, so
checking only the FINAL FlowState at the end of the walk -- the way
warn_result_not_always_set otherwise does -- would miss a path that itself
never assigns the result before leaving through Exit: it is dropped from
the merge precisely because nothing after the join is reachable along it,
which says nothing about whether IT was fine on its own.  Sema.h's
FlowResultMaybeUnset_ is the separate, sticky check for that, made where
the Exit itself is walked rather than only at the join.  Here the Cond-true
path calls a bare Exit having never assigned F at all, which is a real bug
this must keep catching.
*)

(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: does not assign to its result on every path through it
*)

program p;
function F(Cond: Boolean): Integer;
begin
  if Cond then
    Exit
  else
    F := 2
end;
var r: Integer;
begin
  r := F(true)
end.
