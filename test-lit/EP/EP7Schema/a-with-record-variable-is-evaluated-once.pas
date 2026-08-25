(*
RUN: %plang -std=iso10206 -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:calls=1 a2=42
*)

program p(output);
type inner = record x: integer end;
     t(n: integer) = record a: array[1..n] of inner end;
var q: ^t; i, calls: integer;
function idx: integer;
begin calls := calls + 1; idx := 2 end;
begin new(q, 5); calls := 0;
  for i := 1 to 5 do q^.a[i].x := i;
  with q^.a[idx] do x := 42;
  writeln('calls=', calls:1, ' a2=', q^.a[2].x:1) end.
