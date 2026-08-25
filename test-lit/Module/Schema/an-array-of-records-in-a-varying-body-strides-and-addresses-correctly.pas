(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[xy]5 [xy]10 [xy]15 
*)

program p(output);
type t(n: integer) =
       record a: array[1..n] of record s: string(n); k: integer end end;
var q: ^t; i: integer;
begin
  new(q, 3);
  for i := 1 to 3 do begin q^.a[i].s := 'xy'; q^.a[i].k := i * 5 end;
  for i := 1 to 3 do write('[', q^.a[i].s, ']', q^.a[i].k:1, ' ');
  writeln; dispose(q)
end.
