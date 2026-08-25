(*
RUN: %plang -std=iso10206 -frange-checks %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: out of bounds 1..4
*)

program p(output);
type vec(n: integer) = array[1..n] of integer;
     v2(n: integer)  = vec(n);
var x: v2(4); i: integer;
begin i := 0; x[i] := 1; writeln('not reached') end.
