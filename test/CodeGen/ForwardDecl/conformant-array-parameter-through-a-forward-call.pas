(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:6
*)

program p;
var arr: array[1..3] of integer;
function total(var a: array[lo..hi: integer] of integer): integer; forward;
function go(var a: array[lo..hi: integer] of integer): integer;
begin go := total(a) end;
function total;
var i, s: integer;
begin s := 0; for i := lo to hi do s := s + a[i]; total := s end;
begin arr[1] := 1; arr[2] := 2; arr[3] := 3; writeln(go(arr)) end.
