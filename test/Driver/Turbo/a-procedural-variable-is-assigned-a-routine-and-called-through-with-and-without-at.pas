(*
Turbo procedural TYPES and VALUES: 'type TProc = procedure(x: integer);'
declares a procedural type, and 'f: TProc' a VARIABLE of it -- neither has
any ISO 7185/Extended Pascal equivalent (only the procedural PARAMETER form,
ISO Sec6.6.3.1, which is unaffected by this feature and stays exercised
elsewhere, e.g. test/Module/ProcParams).  f is assigned two different
top-level (non-nested) procedures in turn, once with the bare name and once
with an explicit '@' -- Tier 1's general address-of operator, optional here
-- and each assignment is followed by a call THROUGH f to confirm it really
dispatches to the routine just stored rather than to whichever one ran
first, and that the argument travels correctly.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:first:10
CHECK-NEXT:second:20
*)

program p;

type
  TProc = procedure(x: integer);

var
  f: TProc;

procedure First(x: integer);
begin
  writeln('first:', x);
end;

procedure Second(x: integer);
begin
  writeln('second:', x);
end;

begin
  f := First;
  f(10);
  f := @Second;
  f(20);
end.
