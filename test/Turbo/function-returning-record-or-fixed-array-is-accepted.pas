(*
Issue #585: `-std=turbo` wrongly rejected a function returning a `record` or
a fixed-size `array` type ("error: a function result must be a simple or
pointer type"), even though this is ordinary, idiomatic Turbo Pascal 7 --
confirmed against a local `fpc -Mtp` install, which accepts and runs both
forms below.  Real TP7's actual restriction is narrower: it disallows an
OPEN array result (and any file-containing result), not record/fixed-array
results in general.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2
CHECK-NEXT:1 2 3
*)

program t;
type
  R   = record X, Y: Integer; end;
  Arr = array[1..3] of Integer;

function MakeR(A, B: Integer): R;
begin
  MakeR.X := A;
  MakeR.Y := B;
end;

function MakeArr(A, B, C: Integer): Arr;
begin
  MakeArr[1] := A;
  MakeArr[2] := B;
  MakeArr[3] := C;
end;

var
  Q1: R;
  Q2: Arr;
begin
  Q1 := MakeR(1, 2);
  Writeln(Q1.X, ' ', Q1.Y);
  Q2 := MakeArr(1, 2, 3);
  Writeln(Q2[1], ' ', Q2[2], ' ', Q2[3]);
end.
