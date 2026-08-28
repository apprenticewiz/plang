(*
Real Turbo Pascal's case is not exhaustive-or-die the way ISO 7185/Extended
Pascal's is (see unmatched-case-reports.pas under CodeGen/RuntimeChecks,
unaffected by this): a selector matching no case-constant, with no else/
otherwise part either, just does nothing and execution carries on after the
case-statement.  Every arm is labeled so that running it would be visible
("WRONG-..."), proving falling through is not confused with running some
arm's body -- only 'after' must appear, and the exit code must be 0, not a
runtime-error trap.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK-NOT:WRONG
CHECK:after
*)

program p;
var
  x: integer;
begin
  x := 99;
  case x of
    1: writeln('WRONG-one');
    2: writeln('WRONG-two')
  end;
  writeln('after')
end.
