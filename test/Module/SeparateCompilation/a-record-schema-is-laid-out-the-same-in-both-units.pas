(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2 2.25 904.0
*)

//--- mod.pas
module Shp interface;
export Shp = (poly, mk);
type poly(n: integer) = record deg: integer; c: array[0..n] of real end;
procedure mk(var q: poly(2));
end;
procedure mk(var q: poly(2));
var i: integer;
begin q.deg := 2; for i := 0 to 2 do q.c[i] := i + 0.25 end;
end.

//--- prog.pas
program p(output);
import Shp;
var a: poly(2); b: poly(4); i: integer;
begin
  mk(a);
  for i := 0 to 4 do b.c[i] := 900 + i;
  writeln(a.deg:0, ' ', a.c[2]:0:2, ' ', b.c[4]:0:1)
end.
