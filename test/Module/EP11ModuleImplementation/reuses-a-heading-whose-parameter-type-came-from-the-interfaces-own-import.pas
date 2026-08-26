(*
EP §6.11.1: an implementation module may repeat a routine heading as its bare
name alone ("function DoubleIt;"), borrowing the parameter and result types
the interface already gave it.  When one of those types came from an import
the INTERFACE made for itself (here, Colors.Meters, imported un-qualified as
just Meters) -- an import the implementation never repeats and so cannot see
on its own -- re-checking the borrowed heading used to look "Meters" up
again from scratch and fail to find it, even though this is one file with no
separate compilation involved at all.  A var of the same borrowed type
(Distance) already worked, since a variable's resolved type is carried
across as-is; a routine's heading was re-resolved by name and so is the one
this file targets.

RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

module Colors interface;
export Colors = (Meters);
type Meters = 0..1000;
end.
module Colors;
end.

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

program p;
import Palette;
begin
  writeln(DoubleIt(Distance))
end.
