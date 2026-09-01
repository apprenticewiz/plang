(*
Issue #543's general-case stress test, part 2: a named procedural parameter
type (IntOp) alongside Turbo's own open-array parameter form (`array of
Integer`) in the same call -- both go through CodeGenProcs.cpp's own
parameter loop and paramMeta_ population, so this exercises that the
procedural-parameter fix does not disturb the (already-working, unrelated)
open-array-parameter arm beside it, and that the two combine correctly in
one call: SumApplied receives 'op' (a bare routine name, taking its
address through the fixed procType detection) and 'xs' (an open array)
side by side.

Square(1)+Square(2)+Square(3)+Square(4) = 1+4+9+16 = 30.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:30
*)

program NamedProcParamWithOpenArray;
type
  IntOp = function(a: Integer): Integer;

function Square(a: Integer): Integer;
begin
  Square := a * a;
end;

function SumApplied(op: IntOp; const xs: array of Integer): Integer;
var
  i, total: Integer;
begin
  total := 0;
  for i := Low(xs) to High(xs) do
    total := total + op(xs[i]);
  SumApplied := total;
end;

var
  data: array[1..4] of Integer;
begin
  data[1] := 1; data[2] := 2; data[3] := 3; data[4] := 4;
  Writeln(SumApplied(Square, data));
end.
