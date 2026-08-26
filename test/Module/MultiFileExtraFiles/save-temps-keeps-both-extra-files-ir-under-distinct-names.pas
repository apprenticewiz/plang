(*
Each extra file's -save-temps IR names its own function --
pas_<module>$<Function>, module name lowercased -- proving the second
extra file's IR did not overwrite the first's under a name they both
would otherwise have shared.

RUN: split-file %s %t.dir
RUN: cd %t.dir && %plang -std=iso10206 -save-temps main.pas unitA/foo.pas unitB/foo.pas -o prog
RUN: FileCheck --check-prefix=UNITA %s < %t.dir/unitA_foo.ll
RUN: FileCheck --check-prefix=UNITB %s < %t.dir/unitB_foo.ll
*)

(*
UNITA: pas_unita$F
UNITB: pas_unitb$G
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
