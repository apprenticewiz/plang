(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 3
*)

//--- mod.pas
module Shapes interface;
export Shapes = (Point, MakePoint);
type Point = record x, y: integer end;
function MakePoint(a: integer; b: integer): integer;
end.
module Shapes;
type Point = record x, y: integer end;
function MakePoint(a: integer; b: integer): integer;
begin MakePoint := a + b end;
end.

//--- prog.pas
program p;
import Shapes;
var pt: Point;
begin
  pt.x := 3; pt.y := 4;
  writeln(pt.x + pt.y, ' ', MakePoint(1, 2))
end.
