(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5 9
*)

program p(output);
type A(k: integer) = record a: array[1..k] of integer; id: integer end;
     B(n: integer) = A(n);
var x: B(6);
begin with x do begin id := 5; a[3] := 9 end;
  writeln(x.id:1, ' ', x.a[3]:1) end.
