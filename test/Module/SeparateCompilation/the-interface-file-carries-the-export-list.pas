(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:36 1
*)

//--- mod.pas
module SepRenameMod interface;
export SepRenameMod = (Squ => Sq2, protected Calls);
var Calls: integer;
function Squ(x: integer): integer;
end.
module SepRenameMod;
var Calls: integer;
    Hidden: integer;
function Squ(x: integer): integer;
begin Calls := Calls + 1; Squ := x * x end;
to begin do begin Calls := 0; Hidden := 0 end;
end.

//--- prog.pas
program p;
import SepRenameMod (Sq2 => Sqr2);
begin writeln(Sqr2(6), ' ', Calls) end.
