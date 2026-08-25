(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

//--- mod.pas
module Arith;
function Double(x: integer): integer;
begin Double := x * 2 end;
end.

//--- prog.pas
program p;
import Arith;
var n: integer;
begin
  n := Double(21);
  writeln(n)
end.
