(*
Real Turbo Pascal's default is {$B-}: `and`/`or` on two Boolean operands
short-circuit, the same shape Extended Pascal's and_then/or_else always
use (CGBinaryOps::emitShortCircuit, factored out of that existing lowering
and reused here).  `false and X` can never be true regardless of what X
is, so a correct short-circuit must never evaluate X at all -- proven here
with an observable side effect (a writeln) inside the right operand that
must not appear in the output.  No {$B} directive at all: {$B-} is
Turbo's starting point, the same way -std=turbo alone already starts with
{$R-} (see range-checks-default-off-under-turbo-lets-an-out-of-range-
write-through.pas).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:b=false
*)

program and_short_circuit;

function SideEffect: Boolean;
begin
  writeln('SIDEEFFECT CALLED');
  SideEffect := true
end;

var b: Boolean;
begin
  b := false and SideEffect;
  writeln('b=', b)
end.
