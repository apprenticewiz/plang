(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: not %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: protected
*)

//--- mod.pas
module SepProtectedMod interface;
export SepProtectedMod = (protected Calls);
var Calls: integer;
end.
module SepProtectedMod;
var Calls: integer;
end.

//--- prog.pas
program p;
import SepProtectedMod;
begin Calls := 3 end.
