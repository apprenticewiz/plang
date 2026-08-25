(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: not %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Hidden
*)

//--- mod.pas
module SepHiddenMod interface;
export SepHiddenMod = (Squ);
function Squ(x: integer): integer;
end.
module SepHiddenMod;
var Hidden: integer;
function Squ(x: integer): integer; begin Squ := x * x end;
end.

//--- prog.pas
program p;
import SepHiddenMod;
begin writeln(Hidden) end.
