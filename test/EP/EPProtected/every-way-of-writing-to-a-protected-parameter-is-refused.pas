(*
EP section 6.7.3.1.  The check walked nested INDEX expressions only, and ran
from the assignment statement alone -- so of the four ways a program
writes to a variable, one was caught:

  arr[1] := 7   caught      (an index path)
  r.f := 5      NOT caught  (a field path reaches the same storage)
  read(r.f)     NOT caught  (section 6.9.1 makes read into an assignment)
  bumpI(r.f)    NOT caught  (a var-parameter actual is written by callee)
*)

(*
RUN: not %plang_ep %s -o %t 2> %t.err
RUN: grep -c 'protected parameter' %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
COUNT:4
*)

program p(output);
type rec = record f: integer end;
var g: rec; a: array[1..3] of integer;
procedure bumpI(var x: integer); begin x := 99 end;
procedure q(protected r: rec; protected arr: array[1..3] of integer);
begin
  r.f := 5;
  bumpI(r.f);
  read(r.f);
  arr[1] := 7
end;
begin g.f := 1; q(g, a); writeln(g.f:1) end.
