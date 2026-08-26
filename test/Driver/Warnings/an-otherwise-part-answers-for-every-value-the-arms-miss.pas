(*
RUN: %plang -std=iso10206 %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: does not cover
*)

program p(output);
type c = (red, green, blue);
var x: c;
begin x := red; case x of red: writeln(1); otherwise end end.
