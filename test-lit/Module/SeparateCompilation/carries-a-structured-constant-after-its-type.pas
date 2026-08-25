(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:12
*)

//--- mod.pas
module Places interface;
export Places = (Spot, Origin);
type Spot = record x, y: integer end;
const Origin = Spot[x: 1; y: 2];
end;
end.

//--- prog.pas
program p(output);
import Places;
var q: Spot;
begin q := Origin; writeln(q.x, q.y) end.
