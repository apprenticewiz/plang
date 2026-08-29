(*
Turbo's open-array parameter follows ordinary Pascal value/var semantics,
exactly like any other array parameter -- there is nothing implicitly
'var' about `a: array of Integer` -- confirmed empirically against a local
fpc -Mtp build: a plain (value) open-array parameter that modifies its own
elements does NOT write back to the caller's actual, while `var a: array of
Integer` does.  This falls out of the SAME machinery EP's own by-value
conformant-array parameter already uses (Sema::recordModifiedParams marks
which formals the body writes to; CodeGenProcs.cpp's prologue heap-copies a
by-value one only when modified) -- reused here rather than reimplemented,
so this is as much a regression guard on that reuse as it is a new-feature
test.  Also confirms two names sharing one group (`a, b: array of Integer`
is not written here, but the SAME independent-length guarantee -- see
open-array-parameter-sums-arrays-of-different-lengths.pas -- combines with
this file's value/var distinction without interfering).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --match-full-lines %s
*)

program p;
procedure ZeroVal(a: array of Integer);
var i: Integer;
begin
  for i := Low(a) to High(a) do a[i] := 0;
end;

procedure ZeroVar(var a: array of Integer);
var i: Integer;
begin
  for i := Low(a) to High(a) do a[i] := 0;
end;

var
  x3: array[1 .. 3] of Integer;
  i: Integer;
begin
  x3[1] := 1; x3[2] := 2; x3[3] := 3;
  ZeroVal(x3);
  for i := 1 to 3 do write(x3[i], ' ');
  writeln;
  ZeroVar(x3);
  for i := 1 to 3 do write(x3[i], ' ');
  writeln;
end.

(*
CHECK:1 2 3
CHECK-NEXT:0 0 0
*)
