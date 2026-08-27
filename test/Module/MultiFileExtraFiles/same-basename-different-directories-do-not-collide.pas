(*
Two "extra" files sharing a basename (unitA/foo.pas, unitB/foo.pas)
disambiguate their .o output by directory, not just basename -- issue #20.

-save-temps, so the disambiguated names are actually still on disk to check
afterward: without it, an extra file's .o is only ever a link-step
intermediate the driver deletes once the link is done with it, the same as
the main file's own object (issue #279) -- this test's own concern is the
*naming* (that two extra files sharing a basename get two distinct object
paths, not one clobbering the other), which -save-temps still exercises
just as well while also leaving something for the RUN lines below to find.

RUN: split-file %s %t.dir
RUN: cd %t.dir && %plang -std=iso10206 -save-temps main.pas unitA/foo.pas unitB/foo.pas -o prog
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
