(*
RUN: %plang %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: div by zero
*)

program p;
var a, b: integer;
begin a := 10; b := 0; writeln('before'); writeln(a div b) end.
