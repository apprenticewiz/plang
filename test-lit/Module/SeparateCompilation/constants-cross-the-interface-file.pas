(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5 hi 2 9
*)

//--- mod.pas
module Limits interface;
export Limits = (MaxRows, Greeting, Half, Row);
const MaxRows = 5;
      Greeting = 'hi';
      Half = MaxRows div 2;
type Row = array [1..MaxRows] of integer;
end;
end.

//--- prog.pas
program p(output);
import Limits;
var r: Row;
begin r[MaxRows] := 9;
  writeln(MaxRows, ' ', Greeting, ' ', Half, ' ', r[5]) end.
