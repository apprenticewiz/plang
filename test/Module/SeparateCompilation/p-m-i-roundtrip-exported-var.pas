(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
*)

//--- mod.pas
module Counter;
var Count: integer;
procedure Increment;
begin Count := Count + 1 end;
end.

//--- prog.pas
program p;
import Counter;
begin
  Count := 0;
  Increment;
  Increment;
  writeln(Count)
end.
