(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[abcdefghijkl] 12 5150
*)

program p(output);
type s(m: integer) = string(m);
     t(n: integer) = record f: s(n); tail: integer end;
var q: ^t;
begin new(q, 12); q^.tail := 5150; q^.f := 'abcdefghijkl';
  writeln('[', q^.f, '] ', length(q^.f):1, ' ', q^.tail:1) end.
