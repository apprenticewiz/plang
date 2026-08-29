(*
Turbo Assigned(p): true iff p is not nil.  Extended here to a procedural
value as well as an ordinary pointer -- f starts out unassigned (plang
zero-initializes local variables, so an unassigned procedural variable is
nil the same way an unassigned pointer is), is given a routine and reports
true, then is cleared back to nil with 'f := nil' and reports false again.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:not-assigned
CHECK-NEXT:assigned
CHECK-NEXT:not-assigned
*)

program p;

type
  TProc = procedure(x: integer);

var
  f: TProc;

procedure DoIt(x: integer);
begin
  writeln('doit:', x);
end;

procedure Report;
begin
  if Assigned(f) then
    writeln('assigned')
  else
    writeln('not-assigned');
end;

begin
  Report;
  f := DoIt;
  Report;
  f := nil;
  Report;
end.
