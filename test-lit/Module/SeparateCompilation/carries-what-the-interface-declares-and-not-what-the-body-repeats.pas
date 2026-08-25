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
module Plain interface;
export Plain = (pt, twice);
type pt = record x, y: integer end;
function twice(v: integer): integer;
end;
function twice;
begin twice := v + v end;
end.

//--- prog.pas
program p(output);
import Plain;
var q: pt;
begin q.x := 21; writeln(twice(q.x)) end.
