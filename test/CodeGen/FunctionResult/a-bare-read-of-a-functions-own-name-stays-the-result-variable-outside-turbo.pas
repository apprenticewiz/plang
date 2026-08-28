(*
The Turbo-only reversal in
test/Driver/Turbo/a-recursive-function-called-by-its-own-bare-name-actually-recurses.pas
does not apply here: no -std= flag on the RUN line means ISO 7185, and
IdentExpr::IdentResolution's own comment records that ISO 7185/Extended
Pascal deliberately keep this project's older, simpler
(if not ISO-§6.8.2.2-literal) reading, where EVERY bare occurrence of a
function's own name inside its body -- assignment target or plain read
alike -- denotes the result variable, never a recursive call.

Same construct, same trace, as the Turbo test this mirrors: Cnt(N=5)
decrements N once (5 -> 4), takes the else branch, and reads its OWN
result cell (0, its zero-initialized starting value) rather than
recursing -- Cnt+1 = 1, and N is left at 4, having been decremented only
the once.
*)

(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck %s
*)

program isonorecurse;
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
CHECK:1
CHECK-NEXT:4
*)
