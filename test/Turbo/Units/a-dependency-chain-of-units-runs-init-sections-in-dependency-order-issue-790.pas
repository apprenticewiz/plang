(*
Issue #790: when unit A itself 'uses' unit B (A's own interface 'uses'
here), and a program 'uses A, B' (B named again, redundantly, at the
program's own top level, in the OPPOSITE position from dependency order --
deliberately, to prove the actual runtime order comes from the real
dependency graph, not from the program's own 'uses' list order), B's own
initialization section must run before A's, and A's before the program's
own top-level `begin`.  Real `fpc -Mtp` behavior: each unit's own init
function brings up what it depends on first (depth-first over its own
'uses' list) -- see this repo's docs/turbo.md for the full rule.  A and B
are each compiled completely separately (`-c`) with their sources deleted
before the program's own compile, the same genuine-separate-compilation
proof the sibling issue-790 test uses.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -I%t.dir -c %t.dir/unitb.pas -o %t.dir/unitb.o
RUN: %plang -std=turbo -I%t.dir -c %t.dir/unita.pas -o %t.dir/unita.o
RUN: rm %t.dir/unita.pas %t.dir/unitb.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/unita.o %t.dir/unitb.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:B init
CHECK-NEXT:A init, BRan=1
CHECK-NEXT:main, ARan=2 BRan=1
*)

//--- unitb.pas
unit UnitB;

interface

var BRan: Integer;

implementation

begin
  BRan := 1;
  Writeln('B init');
end.

//--- unita.pas
unit UnitA;

interface

uses UnitB;

var ARan: Integer;

implementation

begin
  ARan := BRan + 1;
  Writeln('A init, BRan=', BRan);
end.

//--- main.pas
program ChainProg;
uses UnitA, UnitB;
begin
  Writeln('main, ARan=', ARan, ' BRan=', BRan);
end.
