(*
Combines both fixes: unitA/foo.pas and unitB/foo.pas share a basename
AND unitB imports unitA, so both the .o-disambiguation fix and the
sibling-search-path fix are exercised by the same pair of files.

RUN: split-file %s %t.dir
RUN: cd %t.dir && %plang -std=iso10206 main.pas unitA/foo.pas unitB/foo.pas -o prog
RUN: %run %t.dir/prog | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
*)

//--- unitA/foo.pas
module UnitA;
function F: integer;
begin F := 1 end;
end.

//--- unitB/foo.pas
module UnitB;
import UnitA;
function G: integer;
begin G := F + 1 end;
end.

//--- main.pas
program p;
import UnitB;
begin writeln(G) end.
