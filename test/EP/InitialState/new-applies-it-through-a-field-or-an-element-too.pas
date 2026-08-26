(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 7 7
*)

program p(output);
type node = record x: integer value 7 end;
     pn = ^node;
     holder = record p: pn end;
var h: holder; q: pn; a: array[1..2] of pn;
begin new(q); new(h.p); new(a[1]);
  writeln(q^.x, ' ', h.p^.x, ' ', a[1]^.x) end.
