(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: schema discriminant n differs between the target (100000000) and the value (1)
*)

program p(output);
type outer(n: integer) = record
       a: array[1..n] of integer;
       k: integer
     end;
var q, r: ^outer;
begin
  new(q, 1); new(r, 100000000);
  q^.a[1] := 42; r^.k := 777;
  r^.a := q^.a;
  writeln(r^.k)
end.
