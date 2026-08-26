(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: outside the range 0 to 9999
*)

program p(output);
label 10000;
begin 10000: writeln('x') end.
