(*
ISO 7185 §6.8.2.2's real rule (restored here for -std=turbo only -- see
IdentExpr::IdentResolution's own comment for why ISO 7185/Extended Pascal
keep this project's older, simpler-but-not-ISO-correct reading for now):
the assignment-statement grammar is the ONLY place a bare function-
identifier denotes the result variable ('Cnt' on the LEFT of ':=' below).
Everywhere else, including THIS SAME STATEMENT'S OWN RIGHT-HAND SIDE, a
bare function-identifier is §6.7.3's function-designator -- a call, with
zero actual parameters since none were written, and (found here from
INSIDE Cnt's own body) a recursive one.

Traced by hand: Cnt(N=5) decrements N to 4, takes the else branch, and
recurses; Cnt(N=4)->Cnt(N=3)->Cnt(N=2)->Cnt(N=1), which decrements N to 0
and returns 0 directly.  Unwinding adds 1 at each of the 4 levels above
that: Cnt(N=1)=0+1=1, Cnt(N=2)=1+1=2, Cnt(N=3)=2+1=3, Cnt(N=4)=3+1=4 --
the ORIGINAL call's own result.  N itself, a global, is left at 0: every
one of the 5 activations (N=5 down through N=1) ran its own 'N := N - 1'
exactly once.  Before this fix, a bare read of Cnt's own name read the
CURRENT, not-yet-recursed value of its own result cell instead (Turbo
Pascal's real, simpler behaviour, and what this project did for every
dialect until now) -- N only ever decremented once (5 -> 4) and the
answer was 1, not 4.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

program turborecurse;
var N: integer;
function Cnt: integer;
begin
  N := N - 1;
  if N <= 0 then
    Cnt := 0
  else
    Cnt := Cnt + 1;
end;
begin
  N := 5;
  writeln(Cnt);
  writeln(N);
end.

(*
CHECK:4
CHECK-NEXT:0
*)
