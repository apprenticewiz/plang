(*
Issue #746: `plang -std=turbo -c MathUtils.pas` writes its default object
file preserving the SOURCE FILE's own case (Driver::defaultOutput's stem()
never lowercases it) -- MathUtils.o, capital M -- while the unit's own .tui
is always written lowercase (mathutils.tui, writeTUIFile's own
toLower(Unit.Name)) regardless of how the source file itself was named.
findShippedUnitObject (and the transitive-uses auto-link worklist that
calls it, both from issue #705/#708's own PR #745) used to try ONLY the
exact lowercase object name next to the resolved .tui, so a normally-
capitalized unit's own object -- essentially every hand-written unit's,
since MathUtils/Crt/SerB-style capitalization is the overwhelmingly common
convention -- was silently never found, and the final link failed with an
undefined symbol even though `-I .` correctly resolved the unit's
INTERFACE.

This mirrors the issue's own exact repro: MathUtils.pas is compiled to an
object that keeps the source file's own normal capitalization -- exactly
what `plang -std=turbo -c MathUtils.pas` writes by default (Driver.cpp's
defaultOutput/stem() never lowercases it) -- named explicitly here only so
the object lands beside the interface inside %t.dir regardless of lit's own
RUN-line working directory, not to change what case it is written in. The
final link then names only the unit via `uses MathUtils;`, with no .o ever
given on the command line, so auto-linking has to find MathUtils.o itself.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/MathUtils.pas -o %t.dir/MathUtils.o
RUN: %plang -std=turbo -I %t.dir %t.dir/main.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:25
*)

//--- MathUtils.pas
unit MathUtils;

interface

function Square(x: Integer): Integer;

implementation

function Square(x: Integer): Integer;
begin
  Square := x * x;
end;

end.

//--- main.pas
program p;
uses MathUtils;
begin
  Writeln(Square(5));
end.
