(*
Regression gate for the disambiguation this feature adds: 'f := g' where g
bare-names a routine reads as ISO Sec6.7.3's implicit zero-argument call
UNLESS the ASSIGNMENT TARGET's own type is itself callable (Procedure/
Function) -- and an ordinary 'f: integer' never is, under any dialect, so
this must keep meaning exactly what it always has: call Answer, assign its
result.  This is the ubiquitous "assign a function's own result via its own
name" idiom used throughout ISO/EP/Turbo code, and the one this feature
must not disturb -- see checkAssign's own comment (SemaStmt.cpp) for the
disambiguation rule itself.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p;

var
  f: integer;

function Answer: integer;
begin
  Answer := 42;
end;

begin
  f := Answer;
  writeln(f);
end.
