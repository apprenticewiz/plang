(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:121
CHECK-NEXT:100
CHECK-NEXT:120
*)

program p(output);
type t(n: integer) = record s: string(n) end;
var q: ^t; x: string(400); i: integer;
begin new(q, 120); q^.s := '';
  for i := 1 to 120 do q^.s := q^.s + 'y';
  x := trim(q^.s) + 'Z';         writeln(length(x):1);
  x := substr(q^.s, 5, 100);     writeln(length(x):1);
  x := substr(trim(q^.s), 1, 120); writeln(length(x):1) end.
