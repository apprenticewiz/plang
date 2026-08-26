(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

//--- test.pas
module Arith interface;
export Arith = (Twice);
type Row = array[1..3] of integer;
function Twice(x: integer): integer;
end.
module Arith implementation;
function Twice(x: integer): integer;
var r: Row;
begin r[1] := x + x; Twice := r[1] end;
end.
program p(output);
import Arith;
begin writeln(Twice(21)) end.
