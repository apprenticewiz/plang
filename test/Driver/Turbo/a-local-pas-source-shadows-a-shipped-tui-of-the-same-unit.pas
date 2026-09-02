(*
Issue #700: loadUnitInterfaceExports's search over SearchDirs (Sema.cpp) has
to be ONE pass trying both a .tui and a .pas in each directory before
moving to the next directory -- not two separate passes (all dirs' .tui,
THEN all dirs' .pas). The old two-pass shape let a LESS specific directory's
published .tui beat a MORE specific directory's own .pas, inverting the
shadowing order UnitSearchPaths's own comment promises ("the current
directory is meant to shadow the shipped RTL, not the other way around").

This test proves it the sharpest way available: a higher-priority -I
directory holds only PlaceholderUnit's own SOURCE (no .tui at all), while a
lower-priority PLANG_UNIT_DIR directory holds a published .tui for a
same-named unit that disagrees on what Answer is. Under the old two-pass
search, the PLANG_UNIT_DIR .tui is found in pass 1 (scanning every
directory's .tui first) before pass 2 ever looks at the -I directory's .pas,
so the RTL copy would win despite being strictly less specific. This test
must observe the -I directory's own source instead.

RUN: split-file %s %t.dir
RUN: mkdir -p %t.dir/local %t.dir/rtl
RUN: mv %t.dir/local-unit.pas %t.dir/local/placeholderunit.pas
RUN: mv %t.dir/rtl-unit.pas %t.dir/rtl/placeholderunit.pas
RUN: %plang -std=turbo -c %t.dir/rtl/placeholderunit.pas -o %t.dir/rtl/placeholderunit.o
RUN: env PLANG_UNIT_DIR=%t.dir/rtl %plang -std=turbo -I%t.dir/local %t.dir/main.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
*)

//--- local-unit.pas
unit PlaceholderUnit;

interface

const Answer = 1;

implementation

end.

//--- rtl-unit.pas
unit PlaceholderUnit;

interface

const Answer = 2;

implementation

end.

//--- main.pas
program UsesPlaceholderUnit;
uses PlaceholderUnit;
begin
  Writeln(Answer);
end.
