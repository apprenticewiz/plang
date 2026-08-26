(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:15
*)

program p(output);
type vec(n: integer) = array[1..n] of integer;
var v: vec(5); i: integer;
function total(a: array[lo..hi: integer] of integer): integer;
var k, t: integer;
begin t := 0; for k := lo to hi do t := t + a[k]; total := t end;
begin for i := 1 to 5 do v[i] := i; writeln(total(v):1) end.
