(*
TP's Break does not exhaust the for-statement's range the way falling off
the body's natural end does (§6.8.3.9 is about the latter), so it leaves
the control variable holding whatever value it had -- the classic TP
linear-search idiom below relies on reading it right after the loop.
checkDefiniteAssignment's ForStmt arm (SemaFlow.cpp) must not mark i
UndefAfterFor on this account when a Break can reach here, nor may it fall
into reporting the opposite mistake (i "never given a value") by only
removing it from UndefAfterFor without also keeping it in Assigned.
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
var i, n: Integer;
    a: array[1..10] of Integer;
begin
  n := 10;
  for i := 1 to n do
    if a[i] = 5 then break;
  if i <= n then
    writeln('found at ', i)
  else
    writeln('not found')
end.
