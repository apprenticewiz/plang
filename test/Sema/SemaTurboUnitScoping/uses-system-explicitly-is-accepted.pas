(*
Issue #699: 'System' is always the implicit, always-present first scope
pushUnitUsesScopes opens (see its own comment), but real TP7/fpc also accept
it being NAMED explicitly in a uses clause -- confirmed against a local
`fpc -Mtp` build.  There is no "system.tui"/"system.pas" file anywhere for
loadUnitInterfaceExports to find (System's own exports live directly in
Sema::registerBuiltins()'s global scope, not in any unit file at all), so
naming it used to fail with "no unit named 'System' was found" the same way
naming any other nonexistent unit would.  Exercises a program's own uses
clause, a unit's interface uses clause, and a unit's implementation uses
clause -- all three go through pushUnitUsesScopes.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/helper.pas -o %t.dir/helper.o
RUN: %plang -std=turbo -I%t.dir %t.dir/prog.pas %t.dir/helper.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:ok
*)

//--- helper.pas
unit Helper;

interface
uses System;

function DoubleIt(X: Integer): Integer;

implementation
uses System;

function DoubleIt(X: Integer): Integer;
begin
  DoubleIt := X * 2;
end;

end.

//--- prog.pas
program TestCase;

uses System, Helper;

begin
  if DoubleIt(21) = 42 then writeln('ok') else writeln('fail');
end.
