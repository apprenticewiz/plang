(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR-DAG: nothing can be assigned
ERR-DAG: can only be passed as a parameter
*)

program p(output);
type rw = record f1: integer end;
     w = restricted rw;
var a, b: w; c: rw;
begin a := b; c := a; if a = b then writeln('eq') end.
