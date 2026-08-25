(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 row
CHECK-NEXT:2 row
CHECK-NEXT:3 third
CHECK-NEXT:4 row
*)

program p(output);
type digit = 1..4;
     t(n: integer) = record a: array[digit] of string(n) end;
var q: ^t; i: digit;
begin new(q, 9);
      for i := 1 to 4 do q^.a[i] := 'row';
      q^.a[3] := 'third';
      for i := 1 to 4 do writeln(i:1, ' ', q^.a[i]) end.
