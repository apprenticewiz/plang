(*
FlowLoopBroke_ (Sema.h) is scoped to the innermost loop's own body: each of
flowStmt's four loop arms (While/Repeat/For/ForIn) saves it, resets it to
false, walks its own body, and restores it, so a Break belongs only to the
loop it actually breaks.  Here the Break is inside a while loop nested in
the outer for's body, and breaks THAT while, not the for.

Issue #659: -std=turbo now leaves a for-statement's control variable
well-defined after a normal (non-Break) exit too, exactly the way it
already was after a Break one -- so under Turbo specifically, the outer
for's own outcome (well-defined, no warning) no longer depends on whether
FlowLoopBroke_ leaked out of the inner while or not, and this scenario can
no longer observe a leak that way.  'break' itself is a Turbo-only
statement (err_break_outside_loop under any other dialect), so there is no
other dialect this scenario can move to and keep a Break in it.  What
still matters, and is what this now checks, is that a leaked
FlowLoopBroke_ does not corrupt anything ELSE about the outer for's own
flow state -- i must come out well-defined (matching Turbo's real
semantics) and nothing about the inner while/break's own handling should
produce a diagnostic of its own.
*)

(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: undefined here
ERR-ABSENT-NOT: before it has been given a value
*)

program p;
var i: Integer;
begin
  for i := 1 to 10 do
    while false do
      break;
  writeln('after: ', i)
end.
