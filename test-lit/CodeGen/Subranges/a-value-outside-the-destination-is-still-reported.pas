(*
RUN: %plang %s -o %t
RUN: not %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: out of range
*)

program p(output);
var a: 1..10; b: 1..100;
begin b := 50; a := b; writeln(a:1) end.
