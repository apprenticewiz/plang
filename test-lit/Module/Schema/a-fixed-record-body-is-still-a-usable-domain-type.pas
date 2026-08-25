(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:n=3
*)

program p(output);
type buf(cap: integer) = record n: integer end;
var q: ^buf;
begin new(q, 4); q^.n := 3; writeln('n=', q^.n:1) end.
