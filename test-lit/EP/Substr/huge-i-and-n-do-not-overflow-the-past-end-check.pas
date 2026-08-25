(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: outside a string of length 5
*)

program p;
var t: string(20); i, n: integer;
begin t := 'hello';
 i := 4611686018427387904; n := 4611686018427387905;
 writeln(substr(t, i, n)) end.
