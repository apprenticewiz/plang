(*
The symmetric case of an-if-arm-that-sets-the-result-then-exits-does-not-warn.pas:
here it is the ELSE arm that is dead, with an explicit else present (so
mergeWith's ordinary two-branch path runs, not the else-less shortcut).
Both real paths through F set the result -- Cond true takes the then-arm's
assignment, Cond false Exits having already gone through it too -- so this
must stay quiet, and mergeWith's "this side dead, other live" rule and its
mirror "other side dead, this live" both have to agree on that.
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
    F := 2
  else
    Exit
end;
var r: Integer;
begin
  r := F(true)
end.
