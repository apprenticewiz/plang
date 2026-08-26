(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[abcdefghijklmnopqrst] 12345 4242
*)

program p(output);
type ent(cap: integer) = record name: string(cap); id: integer end;
     tbl(cap: integer) = record e: ent(cap); tail: integer end;
var q: ^tbl;
begin new(q, 20); q^.tail := 4242;
  q^.e.name := 'abcdefghijklmnopqrst'; q^.e.id := 99;
  with q^.e do id := 12345;
  writeln('[', q^.e.name, '] ', q^.e.id:1, ' ', q^.tail:1) end.
