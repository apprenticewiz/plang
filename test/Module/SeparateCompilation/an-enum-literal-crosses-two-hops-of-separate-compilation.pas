(*
A constant or enum literal is a compile-time VALUE, not storage, so an
importer inlines it rather than referencing a symbol.  Compiled as one
invocation, that "importer" role is played by whichever of Colors' and
Palette's own module bodies gets emitted first, and Colors always is
(Codegen walks prog.Modules in import order), so Palette's body already
finds Green in scope.  Under genuine separate compilation, though,
Colors contributes nothing to THIS compilation's prog.Modules at all --
only a loadedInterfaces_ entry read back from colors.pmi -- so Palette's
own body (referencing Green in Pick, below) used to be emitted before
that .pmi's constants were registered, fell through to the
imported-VARIABLE path for the unresolved name, and emitted a reference
to "pasg_colors$Green": a symbol nothing ever defines, since Green was
always meant to be an inlined value.  The link then failed, and it failed
one hop further out too: a third module (this file's program) that only
imports Palette and never mentions Colors by name still needs Palette's
own object file to have linked.

RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/colors.pas -o %t.dir/colors.o
RUN: %plang -std=iso10206 -I%t.dir -c %t.dir/palette.pas -o %t.dir/palette.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/palette.o %t.dir/colors.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:yes
*)

//--- colors.pas
module Colors interface;
export Colors = (Color, Red, Green, Blue);
type Color = (Red, Green, Blue);
end.
module Colors;
end.

//--- palette.pas
module Palette interface;
import Colors;
export Palette = (Pick);
function Pick: Color;
end.
module Palette;
function Pick: Color;
begin Pick := Green end;
end.

//--- prog.pas
program p;
import Colors;
import Palette;
var c: Color;
begin
  c := Pick;
  if c = Green then writeln('yes') else writeln('no')
end.
