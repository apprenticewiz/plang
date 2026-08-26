(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: schema discriminant m differs between the target (100000000) and the value (1)
*)

program p(output);
type inner(m: integer) = array[1..m] of integer;
     outer(n: integer) = record
       a: array[1..n] of integer;
       x: inner(n);
       k: integer
     end;
var q, r: ^outer;
begin
  new(q, 1); new(r, 100000000);
  q^.x[1] := 42; r^.k := 777;
  r^.x := q^.x;
  writeln(r^.k)
end.
