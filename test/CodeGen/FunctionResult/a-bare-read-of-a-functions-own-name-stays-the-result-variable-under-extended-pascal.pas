(*
Extended Pascal's own half of
a-bare-read-of-a-functions-own-name-stays-the-result-variable-outside-turbo.pas
-- see that file's comment for the full trace.  The Turbo-only reversal
(IdentExpr::IdentResolution) is gated on Opts.turbo() specifically, not on
"is this the ISO 7185 default", so Extended Pascal needs its own coverage
rather than inheriting ISO 7185's.
*)

(*
RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck %s
*)

program epnorecurse;
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
