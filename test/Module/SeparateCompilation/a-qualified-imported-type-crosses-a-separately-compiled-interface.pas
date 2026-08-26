(*
ISO 10206 §6.11.6's own worked examples include an interface heading that
names a type imported, qualified, from another module -- the two bugs this
file is a regression test for combine on exactly that shape, and nothing
narrower reproduces both at once:

  (a) Frontend.cpp's .pmi writer dropped an interface's own import-part, so
      an identifier the interface needs from its own imports (here,
      Colors.Meters) is undefined once anything reloads the .pmi instead of
      the source; and
  (b) ParseType.cpp's type-denoter parser never accepted the qualified form
      M.name that the expression and statement parsers already did, so
      "var Distance: Colors.Meters" could not even be parsed the first
      time, let alone survive a round trip through a .pmi.

This is deliberately the separate-compilation path -- Colors compiled to a
.pmi, then Palette compiled against just that .pmi, then a program compiled
against just Palette's .pmi -- since that is the path where a bug in the
.pmi writer, rather than in ordinary same-file Sema, is the one that bites.

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
import Colors qualified;
export Palette = (Distance, DoubleIt);
var Distance: Colors.Meters;
function DoubleIt(x: Colors.Meters): integer;
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
