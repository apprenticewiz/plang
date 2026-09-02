(*
Issue #708: the driver's own findShippedUnitObject used to search the
WHOLE tier list independently for a same-named ".o", with no regard for
which directory Sema actually resolved the unit's INTERFACE from. That let
a stale object in an earlier-searched directory silently outrank a newer
interface (and its own, matching object) in a later-searched directory --
the program would type-check against the new interface but link the old
object.

This test proves the fix the sharpest way available: two -I directories
both publish a unit named Foo, but only the SECOND (d2) has an up-to-date
.tui (d1's own .tui is deleted, simulating a stale build where only the old
.o survives); d1's stale object exports OldProc, d2's current object exports
NewProc. With the interface and object resolved coherently from the same
directory (d2, since d1 has no interface to resolve at all), the program
compiles against NewProc and links d2's object -- never touching d1's o at
all, so a program calling NewProc must run, not fail to link.

RUN: split-file %s %t.dir
RUN: mkdir -p %t.dir/d1 %t.dir/d2
RUN: mv %t.dir/old-unit.pas %t.dir/d1/foo.pas
RUN: mv %t.dir/new-unit.pas %t.dir/d2/foo.pas
RUN: %plang -std=turbo -c %t.dir/d1/foo.pas -o %t.dir/d1/foo.o
RUN: rm %t.dir/d1/foo.tui %t.dir/d1/foo.pas
RUN: %plang -std=turbo -c %t.dir/d2/foo.pas -o %t.dir/d2/foo.o
RUN: %plang -std=turbo -I%t.dir/d1 -I%t.dir/d2 %t.dir/main.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:NEW
*)

//--- old-unit.pas
unit Foo;

interface

procedure OldProc;

implementation

procedure OldProc;
begin
  Writeln('OLD');
end;

end.

//--- new-unit.pas
unit Foo;

interface

procedure NewProc;

implementation

procedure NewProc;
begin
  Writeln('NEW');
end;

end.

//--- main.pas
program UsesFoo;
uses Foo;
begin
  NewProc;
end.
