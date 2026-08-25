(*
RUN: %plang -fno-range-checks %s -o %t
RUN: not %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=OUT %s < %t.out
*)

(*
ERR: dereference of nil
OUT-DAG: past the bound
*)

program p;
type pi = ^integer;
var a: array[1..5] of integer; i: integer; q: pi;
begin
  i := 9; a[i] := 1; writeln('past the bound');
  q := nil; writeln(q^)
end.
