(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: is not usable
*)

program p(output);
type vec(n: integer) = array[1..n] of real;
     vecptr = ^vec;
var q: vecptr;
begin
  new(q, 2305843009213693953); { 2^61 + 1 }
  q^[1] := 1.0;
  q^[2] := 2.0;
  writeln(q^[1])
end.
