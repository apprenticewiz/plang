(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 14 21 
CHECK-NEXT:-5
*)

//--- mod.pas
module Ap interface;
export Ap = (Show, Apply, Neg);
procedure Show(a: array[lo..hi: integer] of integer);
function Apply(function f(x: integer): integer; v: integer): integer;
function Neg(x: integer): integer;
end;
procedure Show(a: array[lo..hi: integer] of integer);
var i: integer;
begin for i := lo to hi do write(a[i], ' '); writeln end;
function Apply(function f(x: integer): integer; v: integer): integer;
begin Apply := f(v) end;
function Neg(x: integer): integer;
begin Neg := -x end;
end.

//--- prog.pas
program p(output);
import Ap;
var a: array[1..3] of integer; i: integer;
begin
  for i := 1 to 3 do a[i] := i * 7;
  Show(a);
  writeln(Apply(Neg, 5))
end.
