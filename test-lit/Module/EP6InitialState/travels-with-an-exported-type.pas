(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:34 7
*)

//--- mod.pas
module Figures interface;
export Figures = (point, counter);
type point = record x, y: integer end value [x: 3; y: 4];
     counter = integer value 7;
end;
end.

//--- prog.pas
program p(output);
import Figures;
var q: point; n: counter;
begin writeln(q.x, q.y, ' ', n) end.
