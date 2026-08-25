(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[abcdefghijklmnopqrst] 20
*)

program p(output);
type B(m: integer) = string(m);
     C(n: integer) = B(n);
var q: ^C;
begin new(q, 20); q^ := 'abcdefghijklmnopqrst';
  writeln('[', q^, '] ', length(q^):1) end.
