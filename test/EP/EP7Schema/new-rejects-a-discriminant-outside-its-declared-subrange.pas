(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: value 500 out of range 1..10
*)

program p(output);
type small = 1..10;
     t(n: small) = array[1..n] of integer;
     vecptr = ^t;
var q: vecptr; i: integer;
begin
  i := 500;
  new(q, i);
  writeln('unreachable')
end.
