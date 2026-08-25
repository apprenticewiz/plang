(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:34
*)

//--- test.pas
module W interface;
export W = (Spot, Corner);
type Spot = record x, y: integer end;
const Corner = Spot[x: 3; y: 4];
end;
end.
program p(output);
import W;
var q: Spot;
begin q := Corner; writeln(q.x, q.y) end.
