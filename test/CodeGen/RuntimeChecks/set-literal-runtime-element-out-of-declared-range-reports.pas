(*
RUN: %plang %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: value 999 out of range 1..10
*)

program p;
var s: set of 1..10; x: integer;
begin
  x := 999;
  s := [x];
  writeln(x in s)
end.
