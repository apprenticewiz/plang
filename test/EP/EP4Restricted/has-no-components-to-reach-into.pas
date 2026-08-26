(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: components are not accessible
*)

program p(output);
type rw = record f1: integer end;
     w = restricted rw;
var a: w;
begin writeln(a.f1) end.
