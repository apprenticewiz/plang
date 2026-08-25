(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: comparison is always
*)

program p(output);
var c: char; b: boolean;
begin c := 'x'; b := false;
  if (c > 'a') and (b = false) then writeln('ok') end.
