(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:15
*)

program p(output);
type A(k: integer) = array[1..k] of integer;
     B(n: integer) = A(n);
procedure sumIt(var arr: array[lo..hi: integer] of integer; var total: integer);
var i: integer;
begin total := 0; for i := lo to hi do total := total + arr[i] end;
var y: B(5); i: integer; s: integer;
begin for i := 1 to 5 do y[i] := i;
  sumIt(y, s); writeln(s:1) end.
