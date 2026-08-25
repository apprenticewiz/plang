(*
Two "extra" files sharing a basename (unitA/foo.pas, unitB/foo.pas)
disambiguate their .o output by directory, not just basename -- issue #20.

RUN: split-file %s %t.dir
RUN: cd %t.dir && %plang -std=iso10206 main.pas unitA/foo.pas unitB/foo.pas -o prog
RUN: test -e %t.dir/unitA_foo.o
RUN: test -e %t.dir/unitB_foo.o
RUN: %run %t.dir/prog | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2
*)

//--- unitA/foo.pas
module UnitA;
function F: integer;
begin F := 1 end;
end.

//--- unitB/foo.pas
module UnitB;
function G: integer;
begin G := 2 end;
end.

//--- main.pas
program p;
import UnitA; UnitB;
begin writeln(F, ' ', G) end.
