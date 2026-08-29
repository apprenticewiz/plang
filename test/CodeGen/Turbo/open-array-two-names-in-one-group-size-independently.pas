(*
`a, b: array of Integer` -- two names sharing ONE parameter group -- must
size INDEPENDENTLY at the call site: confirmed against a local fpc -Mtp
build that a and b each get their own high bound, unlike EP/ISO 7185 Level
1's conformant-array-schema form, where two names sharing one group also
share ONE bound-variable pair (a standards quirk this compiler's existing
machinery already reproduces -- see Sema.cpp's checkProcBody, which
rejects a second name's attempt to redefine the same shared bound as a
duplicate parameter).  Turbo's own form avoids that quirk entirely by
synthesizing a bound pair PER NAME (openArrayLowBoundName/
openArrayHighBoundName, AstType.h) rather than per group -- this is the
direct regression test for that synthesis actually being per-name: if it
were not, either this would fail to compile (a duplicate-bound-name clash,
the EP shape) or SumTwo would silently sum the WRONG number of elements
for one of the two arrays.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --match-full-lines %s
*)

program p;
function SumTwo(a, b: array of Integer): Integer;
var i, s: Integer;
begin
  s := 0;
  for i := Low(a) to High(a) do s := s + a[i];
  for i := Low(b) to High(b) do s := s + b[i];
  SumTwo := s;
end;

var
  x3: array[1 .. 3] of Integer;
  y5: array[0 .. 4] of Integer;
begin
  x3[1] := 1; x3[2] := 2; x3[3] := 3;
  y5[0] := 1; y5[1] := 1; y5[2] := 1; y5[3] := 1; y5[4] := 1;
  writeln(SumTwo(x3, y5));
end.

(*
CHECK:11
*)
