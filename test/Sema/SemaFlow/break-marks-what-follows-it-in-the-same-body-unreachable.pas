(*
Break (Builtins.def) leaves the loop it is written in, so a statement right
after one, in the SAME statement sequence, never runs -- the same
alwaysTransfers (SemaStmt.cpp) reasoning Halt/Exit/RunError get.  What
follows the WHOLE loop is a different question (see
break-leaves-the-for-loops-control-variable-readable.pas): a loop may run
zero times, or complete without ever reaching the Break, so alwaysTransfers
does not, and must not, call anything after the loop itself unreachable on
this account.  The trailing CHECK-NOT below is the negative half of this
test: no second "cannot be reached" appears for the line after the loop.
*)

(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: this statement cannot be reached
CHECK-NEXT: writeln('dead-inside-loop-body')
CHECK-NOT: cannot be reached
*)

program p;
var i: Integer;
begin
  for i := 1 to 10 do
  begin
    break;
    writeln('dead-inside-loop-body')
  end;
  writeln('reachable-after-loop')
end.
