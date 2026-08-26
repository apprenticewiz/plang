(*
EP §6.11.6: an interface's declarations may need a name the interface
itself imports (here, Palette's "var Distance: Meters" and its "DoubleIt"
parameter both need Colors' "Meters"), which is unremarkable when the whole
program is one compilation -- Sema processes the interface's own
import-part before checking its declarations, exactly as it would a module
body's.  Compiled separately, though, nothing of Palette.pas survives but
its .pmi, and until now the .pmi writer dropped the "import Colors" clause
on the floor: reloading palette.pmi left "Meters" an undeclared identifier,
an error a single-file build of the same program never saw.

RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/colors.pas -o %t.dir/colors.o
RUN: %plang -std=iso10206 -I%t.dir -c %t.dir/palette.pas -o %t.dir/palette.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/palette.o %t.dir/colors.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

//--- colors.pas
module Colors interface;
export Colors = (Meters);
type Meters = 0..1000;
end.
module Colors;
end.

//--- palette.pas
module Palette interface;
import Colors;
export Palette = (Distance, DoubleIt);
var Distance: Meters;
function DoubleIt(x: Meters): integer;
end.
module Palette;
function DoubleIt;
begin DoubleIt := x * 2 end;
to begin do Distance := 21;
end.

//--- prog.pas
program p;
import Palette;
begin
  writeln(DoubleIt(Distance))
end.
