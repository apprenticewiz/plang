(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: not %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: 999 is outside the range 1..10; this is an error whenever it is reached
ERR: value 999 out of range 1..10
*)

program p;
var s: set of 1..10;
begin
  s := [999];
  writeln(999 in s)
end.
