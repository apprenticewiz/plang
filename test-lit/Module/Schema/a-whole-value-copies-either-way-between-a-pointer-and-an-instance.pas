(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10 20 30 40 77
CHECK-NEXT:1 2 3 4 88
*)

program p(output);
type t(n: integer) = record a: array[1..n] of integer; k: integer end;
var q: ^t; v: t(4); i: integer;
begin new(q, 4);
      for i := 1 to 4 do q^.a[i] := i * 10;
      q^.k := 77;
      v := q^;
      for i := 1 to 4 do write(v.a[i]:1, ' ');
      writeln(v.k:1);
      for i := 1 to 4 do v.a[i] := i;
      v.k := 88;
      q^ := v;
      for i := 1 to 4 do write(q^.a[i]:1, ' ');
      writeln(q^.k:1) end.
