(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4
*)

//--- mod.pas
module Bd interface;
export Bd = (grid, Board);
type grid = array[1..2, 1..2] of integer;
var Board: grid;
end;
end.

//--- prog.pas
program p(output);
import Bd;
var i, j: integer;
begin
  for i := 1 to 2 do for j := 1 to 2 do Board[i, j] := i * j;
  writeln(Board[2, 2])
end.
