(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot be written
*)

program p(output); type r = record x: integer end;
var q: r; begin writeln(q) end.
