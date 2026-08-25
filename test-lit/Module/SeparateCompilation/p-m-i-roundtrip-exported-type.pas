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
module Types;
type SmallInt = 0..100;
function Clamp(x: integer): SmallInt;
begin
  if x < 0 then Clamp := 0
  else if x > 100 then Clamp := 100
  else Clamp := x
end;
end.

//--- prog.pas
program p;
import Types;
var v: SmallInt;
begin
  v := Clamp(42);
  writeln(v)
end.
