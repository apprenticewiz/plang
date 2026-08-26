(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: discriminant n differs
*)

program p;
type vec(n: integer) = array[1..n] of integer;
var a, b: ^vec;
begin
  new(a, 3); new(b, 5);
  b^ := a^;
  writeln('unreachable')
end.
