(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42 7
*)

program p(output);
type A(k: integer) = record a: array[1..k] of integer; id: integer end;
     B(n: integer) = A(n);
procedure showB(var x: B);
begin x.id := 42; x.a[3] := 7; writeln(x.id:1, ' ', x.a[3]:1) end;
var y: B(6);
begin showB(y) end.
