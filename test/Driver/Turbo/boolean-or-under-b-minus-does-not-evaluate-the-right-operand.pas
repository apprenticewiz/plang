(*
The `or` half of and-under-b-minus-does-not-evaluate-the-right-operand.pas's
own proof: `true or X` can never be false regardless of what X is, so a
correct short-circuit must never evaluate X either -- same observable
side effect, same must-not-appear-in-the-output check.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:b=true
*)

program or_short_circuit;

function SideEffect: Boolean;
begin
  writeln('SIDEEFFECT CALLED');
  SideEffect := true
end;

var b: Boolean;
begin
  b := true or SideEffect;
  writeln('b=', b)
end.
