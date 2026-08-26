(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: outside a string of length 3
*)

program p;
var t: string(20);
begin t := 'abc'; writeln(substr(t, 2, 10)) end.
