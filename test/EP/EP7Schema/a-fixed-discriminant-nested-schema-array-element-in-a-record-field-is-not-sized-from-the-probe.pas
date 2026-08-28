(*
Issue #393's other shape: the array of fixed-discriminant nested schema
instances is a FIELD inside a varying record-bodied schema, rather than the
schema's whole body -- so a field (k) sits behind it, the way
a-schema-instantiated-inside-a-schema-body-is-not-sized-from-the-probe-
record-field.pas puts one behind the discriminant-DEPENDENT shape.  A
mis-sized array element here shows up as k being overwritten rather than
as a wrong element read back.
*)

(*
RUN: %plang_ep -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 12 13 14
CHECK-NEXT:21 22 23 24
CHECK-NEXT:31 32 33 34
CHECK-NEXT:1 2 3
CHECK-NEXT:99
*)

program p(output);
type inner(m: integer) = array[1..m] of integer;
     outer(n: integer) = record a: array[1..n] of integer;
                                 x: array[1..n] of inner(4);
                                 k: integer end;
var q: ^outer; i: integer;
begin new(q, 3);
  for i := 1 to 3 do begin
    q^.a[i] := i;
    q^.x[i][1] := i * 10 + 1; q^.x[i][2] := i * 10 + 2;
    q^.x[i][3] := i * 10 + 3; q^.x[i][4] := i * 10 + 4
  end;
  q^.k := 99;
  for i := 1 to 3 do
    writeln(q^.x[i][1]:1, ' ', q^.x[i][2]:1, ' ', q^.x[i][3]:1, ' ', q^.x[i][4]:1);
  writeln(q^.a[1]:1, ' ', q^.a[2]:1, ' ', q^.a[3]:1);
  writeln(q^.k:1)
end.
