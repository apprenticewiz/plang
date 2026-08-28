(*
Real Turbo Pascal's case-statement else/otherwise part is a STATEMENT LIST
bounded by the case-statement's own 'end', the same shape a 'begin ... end'
block's body has -- not a single statement the way an ordinary case-list-
element's body is (and the way ISO 10206's own 'otherwise' part still is;
see the-semicolon-before-otherwise-is-optional.pas under CodeGen/
CaseStatement, unaffected by this).  Two writelns in the else part, with no
begin/end wrapping either of them, must both run.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:else-a
CHECK-NEXT:else-b
CHECK-NEXT:after
*)

program p;
var
  x: integer;
begin
  x := 99;
  case x of
    1: writeln('one');
    2: writeln('two')
  else
    writeln('else-a');
    writeln('else-b')
  end;
  writeln('after')
end.
