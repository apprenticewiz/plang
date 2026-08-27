(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: discriminant 'n' of schema 'Vec' needs an ordinal value
ERR-ABSENT-NOT: must be a constant expression
*)

program p;
type Vec(n: integer) = record data: array[1..n] of integer end;
var v: Vec(3.5);
begin
  writeln(v.data[1])
end.
