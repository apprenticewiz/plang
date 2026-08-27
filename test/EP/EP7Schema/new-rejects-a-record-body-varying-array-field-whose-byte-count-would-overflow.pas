(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: is not usable
*)

program p(output);
type rec(n: integer) = record data: array[1..n] of real end;
     recptr = ^rec;
var q: recptr;
begin
  new(q, 2305843009213693953); { 2^61 + 1 }
  q^.data[1] := 1.0;
  q^.data[2] := 2.0;
  writeln(q^.data[1])
end.
