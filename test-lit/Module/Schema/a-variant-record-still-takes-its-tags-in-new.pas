(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:r=5
*)

program p(output);
type shape = (circ, rect);
     fig = record case k: shape of circ: (r: integer);
                                   rect: (w, h: integer) end;
var q: ^fig;
begin new(q, circ); q^.r := 5; writeln('r=', q^.r:1); dispose(q, circ) end.
