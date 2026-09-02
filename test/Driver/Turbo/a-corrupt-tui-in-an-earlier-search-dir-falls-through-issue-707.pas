(*
Issue #707: Sema::loadUnitInterfaceExports' search over SearchDirs broke on
the first EXISTING '<name>.tui' candidate (findExact), then hard-errored
(err_malformed_unit) if that candidate did not actually parse -- unlike a
MISSING '<name>.tui', which already correctly falls through to try the next
search directory (or that directory's own companion .pas).  The sibling
.pmi loader (processImports' own tryCandidate, Sema.cpp) already keeps
searching past a broken candidate; loadUnitInterfaceExports now does the
same: a directory whose 'foo.tui' exists but does not parse (or names the
wrong unit) is remembered as a possible error to report, but the search
keeps going, exactly like "does not exist" already did.

RUN: split-file %s %t.dir
RUN: mkdir -p %t.dir/d1 %t.dir/d2
RUN: mv %t.dir/garbage.tui %t.dir/d1/foo.tui
RUN: mv %t.dir/good.pas %t.dir/d2/foo.pas
RUN: %plang -std=turbo -I%t.dir/d1 -I%t.dir/d2 %t.dir/main.pas -o %t.dir/main
RUN: %run %t.dir/main | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:{{^}}42{{$}}
*)

//--- garbage.tui
this is not valid Pascal at all { unterminated

//--- good.pas
unit Foo;
interface
const Answer = 42;
implementation
end.

//--- main.pas
program Main;
uses Foo;
begin
  writeln(Answer);
end.
