(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: nil
*)

program p(output);
type Vec(n: integer) = array[1..n] of integer;
     pv = ^Vec;
var q: pv;
begin q := nil; writeln('before'); q^[1] := 1; writeln('after') end.
