(*
FlowLoopBroke_ (Sema.h) is scoped to the innermost loop's own body: each of
flowStmt's four loop arms (While/Repeat/For/ForIn) saves it, resets it to
false, walks its own body, and restores it, so a Break belongs only to the
loop it actually breaks.  Here the Break is inside a while loop nested in
the outer for's body, and breaks THAT while, not the for -- the outer for
still exhausts its range on every path that reaches after it (the while's
own condition is false to begin with, every time), so i must still be
reported UndefAfterFor the ordinary way.  Without the save/reset/restore
around each loop arm's own body, the inner Break would leak out and wrongly
suppress the outer for's own warning.
*)

(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'i' is undefined here: a for-statement leaves its control variable undefined when it finishes
*)

program p;
var i: Integer;
begin
  for i := 1 to 10 do
    while false do
      break;
  writeln('after: ', i)
end.
