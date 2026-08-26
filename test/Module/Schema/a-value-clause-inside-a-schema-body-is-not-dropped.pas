(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
CHECK-NEXT:hello 7 20
*)

program p(output);
type t(n: integer) = record
       s: string(n); k: integer value 7;
       a: array[1..n] of integer end;
var v: t(20); i: integer;
begin writeln(v.k:1);
      v.s := 'hello'; for i := 1 to 20 do v.a[i] := i;
      writeln(v.s, ' ', v.k:1, ' ', v.a[20]:1) end.
