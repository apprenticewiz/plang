(*
RepeatStmt is the one loop arm in flowStmt (SemaFlow.cpp) that mutates the
incoming FlowState directly rather than merging a separate Body copy back
in, since a repeat always runs its body at least once.  A Break inside it
ends the repeat-statement itself, the same as it would for any other loop,
not whatever encloses it -- so St.Dead is explicitly saved and restored
around the repeat, the same way FlowLoopBroke_ is.  Without that, a Break
here would leave St.Dead set as though the repeat-statement (and the if-arm
around it) never returned, and mergeWith would then drop this whole arm's
own F := 1 from the join with the other arm -- which does not set F at all
-- producing a false "F might not always be set" warning on code where
every real path through F does set it.
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
  F := 1;
  if Cond then
    repeat
      break
    until true
end;
var r: Integer;
begin
  r := F(true)
end.
