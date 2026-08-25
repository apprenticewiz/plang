(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:via pointer / 5
CHECK-NEXT:instance / 6
CHECK-NEXT:via pointer / 5
*)

program p(output);
type t(n: integer) = record s: string(n); k: integer end;
var q: ^t; v: t(20);
begin new(q, 20); q^.s := 'via pointer'; q^.k := 5;
      writeln(q^.s, ' / ', q^.k:1);
      v.s := 'instance'; v.k := 6;
      writeln(v.s, ' / ', v.k:1);
      v := q^;
      writeln(v.s, ' / ', v.k:1) end.
