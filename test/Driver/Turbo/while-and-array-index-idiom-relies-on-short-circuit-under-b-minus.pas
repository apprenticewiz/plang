(*
The canonical idiom `while (i <= high) and (a[i] <> target) do i := i + 1`
that short-circuit evaluation exists for: once `i` runs past the array's
last element, `i <= 5` is false, and a correct short-circuit must never
go on to evaluate `a[i]` at all -- if it did, that would be an out-of-
bounds access.  {$R+} makes the range check that would catch it explicit
(Turbo's own default is {$R-}, see range-checks-default-off-under-turbo-
lets-an-out-of-range-write-through.pas, which would hide the very bug
this file exists to rule out), so this only passes if the short-circuit
genuinely skips `a[6]` rather than merely getting lucky with unchecked
memory.  The sibling file
(b-plus-breaks-the-short-circuit-idiom-with-a-range-check-error.pas)
proves the negative: the identical loop DOES hit the range check once
{$B+} forces full evaluation instead.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:not found, i=6
*)

program idiom_short_circuit;
var arr: array[1..5] of Char;
    i: Integer;
begin
  arr[1] := 'a'; arr[2] := 'b'; arr[3] := 'c'; arr[4] := 'd'; arr[5] := 'e';
  {$R+}
  i := 1;
  while (i <= 5) and (arr[i] <> 'z') do
    i := i + 1;
  writeln('not found, i=', i)
end.
