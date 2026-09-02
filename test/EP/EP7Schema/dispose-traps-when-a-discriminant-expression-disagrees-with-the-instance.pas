(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.out
RUN: FileCheck %s < %t.err
*)

(*
OUT-NOT: unreachable
*)

(*
CHECK: plang runtime: schema discriminant n differs between the target (5) and the value (6)
*)

program p;
type vec(n: integer) = array[1..n] of integer;
var q: ^vec;
begin
  new(q, 5);
  dispose(q, 6);
  writeln('unreachable')
end.
