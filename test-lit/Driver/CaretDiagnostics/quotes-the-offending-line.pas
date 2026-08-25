(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: begin x := notdeclared end.
*)

program p;
var x: integer;
begin x := notdeclared end.
