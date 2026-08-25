(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 70 de
CHECK-NEXT:1 10 ab
CHECK-NEXT:2 20 ab
CHECK-NEXT:5 50 in 99
*)

program p(output);
type buf(cap: integer) = record n: integer; s: string(cap); m: integer end;
     small = buf(4);
     outer = record b: small; tag: integer end;
var d: small; v: array[1..2] of small; o: outer; i: integer;
begin
  d.n := 7; d.s := 'de'; d.m := 70;
  for i := 1 to 2 do begin v[i].n := i; v[i].s := 'ab'; v[i].m := i * 10 end;
  o.b.n := 5; o.b.s := 'in'; o.b.m := 50; o.tag := 99;
  writeln(d.n:1, ' ', d.m:1, ' ', d.s);
  for i := 1 to 2 do writeln(v[i].n:1, ' ', v[i].m:1, ' ', v[i].s);
  writeln(o.b.n:1, ' ', o.b.m:1, ' ', o.b.s, ' ', o.tag:1)
end.
