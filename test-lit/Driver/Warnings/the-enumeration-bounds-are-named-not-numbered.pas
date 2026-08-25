(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: range is red..blue
*)

program p(output);
type c = (red, green, blue);
var x: c;
begin x := red; if x > blue then writeln('never') else writeln('always') end.
