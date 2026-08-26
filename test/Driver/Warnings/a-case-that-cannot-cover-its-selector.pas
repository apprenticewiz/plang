(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: does not cover blue
*)

program p(output);
type c = (red, green, blue);
var x: c;
begin x := red; case x of red: writeln(1); green: writeln(2) end end.
