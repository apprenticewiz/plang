(*
A warning that cannot be turned off individually is one a project with a
house style has to turn off wholesale, so the name is part of the feature
and not decoration.  With no flag at all, the name shows up in the
diagnostic.
*)

(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: before it has been given a value
*)

program p(output);
var i: integer;
begin writeln(i) end.
