(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:abcd 6.0
CHECK-NEXT:12.0
*)

//--- mod.pas
module Vr interface;
export Vr = (shape, kind, circ..rect, mk, area);
type kind = (circ, rect);
     shape = record
       name: packed array[1..4] of char;
       case k: kind of
         circ: (r: real);
         rect: (w, h: real)
     end;
function mk(kk: kind; a, b: real): shape;
function area(s: shape): real;
end;
function mk(kk: kind; a, b: real): shape;
var s: shape;
begin
  s.name := 'abcd'; s.k := kk;
  if kk = circ then s.r := a else begin s.w := a; s.h := b end;
  mk := s
end;
function area(s: shape): real;
begin
  if s.k = circ then area := 3.0 * s.r * s.r
  else area := s.w * s.h
end;
end.

//--- prog.pas
program p(output);
import Vr;
var s: shape;
begin
  s := mk(rect, 2.0, 3.0);
  writeln(s.name, ' ', area(s):0:1);
  s := mk(circ, 2.0, 0.0);
  writeln(area(s):0:1)
end.
