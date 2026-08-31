(*
Turbo Tier 4, Cluster B item 4: the DEFAULT installed-RTL search path.
unitSearchPaths() (plang/Basic/UnitSearchPath.h) is a three-tier resolution
mirroring catalogSearchPaths()'s own -- env-var override (PLANG_UNIT_DIR),
then <exeDir>/../lib/plang/units, then a compiled-in build-tree fallback --
and Sema::loadUnitInterfaceExports appends it to its search list after
Opts.ModuleSearchPaths and ".".  This test exercises the one tier a lit run
against the in-tree build binary can reach directly: the env-var override.
It proves a `uses` clause resolves a unit that lives NOWHERE ModuleSearchPaths
or "." would find it -- no -I, no copy in the current directory -- with only
PLANG_UNIT_DIR pointing at it, the same shape a real installed plang's
<exeDir>/../lib/plang/units tier resolves with no flags at all (that
installed-from-a-real-prefix case is proven at the CI level instead; see
this item's own report -- lit always invokes the in-tree build-dir binary,
which has no "installed" tier of its own to exercise).

RUN: split-file %s %t.dir
RUN: mkdir -p %t.dir/rtl
RUN: mv %t.dir/placeholderunit.pas %t.dir/rtl/placeholderunit.pas
RUN: env PLANG_UNIT_DIR=%t.dir/rtl %plang -std=turbo %t.dir/main.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

//--- placeholderunit.pas
unit PlaceholderUnit;

interface

const Answer = 42;

implementation

end.

//--- main.pas
program UsesPlaceholderUnit;
uses PlaceholderUnit;
begin
  Writeln(Answer);
end.
