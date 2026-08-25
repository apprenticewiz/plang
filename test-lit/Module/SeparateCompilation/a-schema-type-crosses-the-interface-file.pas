(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
*)

//--- mod.pas
module Sch interface;
export Sch = (vector, Sum);
type vector(n: integer) = array[1..n] of integer;
function Sum(var v: vector): integer;
end;
function Sum(var v: vector): integer;
var i, s: integer;
begin s := 0; for i := 1 to v.n do s := s + v[i]; Sum := s end;
end.

//--- prog.pas
program p(output);
import Sch;
var v: vector(4); i: integer;
begin for i := 1 to 4 do v[i] := i; writeln(Sum(v)) end.
