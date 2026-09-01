(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hello][world][deref]
*)

program p(output);
type rec = record s: string(20); n: integer end;
var r: rec; a: array[1..3] of string(20); q: ^rec;
procedure show(t: string(25)); begin write('[', t, ']') end;
begin
  r.s := 'hello'; a[1] := 'world'; new(q); q^.s := 'deref';
  show(r.s); show(a[1]); show(q^.s); writeln
end.
