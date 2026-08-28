(*
mergeWith's dead-side rule drops the arm that Exits from the merge, but
must not go further and drop the warning altogether: the surviving
(Cond-false) path here assigns nothing at all, which is a real bug and has
nothing to do with the dead arm one way or the other.
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
  if Cond then begin F := 1; Exit end;
  writeln('no assignment on this path')
end;
var r: Integer;
begin
  r := F(true)
end.
