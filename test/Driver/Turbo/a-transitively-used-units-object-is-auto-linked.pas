(*
Issue #705: the driver's auto-link scan (Driver.cpp, compile()) has to walk
the TRANSITIVE closure of 'uses' clauses, not just the units named directly
by the program's own sources. If unit A 'uses' unit B in its interface, and
a program 'uses' only A, B's own object file still has to be pulled into the
final link automatically for anything B contributes that A's interface
re-exposes (a function call, here) -- real `fpc` resolves this transitively
too. The old scan only read scanUsesClauseUnitNames on the program's own
sources, so B's .o (deeper.o below) was never found and the link would fail
with an undefined symbol for pas_deeper$DeepValue.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/deeper.pas -o %t.dir/deeper.o
RUN: %plang -std=turbo -I%t.dir -c %t.dir/middle.pas -o %t.dir/middle.o
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:99
*)

//--- deeper.pas
unit Deeper;

interface

function DeepValue: Integer;

implementation

function DeepValue: Integer;
begin
  DeepValue := 99;
end;

end.

//--- middle.pas
unit Middle;

interface

uses Deeper;

function MiddleValue: Integer;

implementation

function MiddleValue: Integer;
begin
  MiddleValue := DeepValue;
end;

end.

//--- main.pas
program UsesMiddle;
uses Middle;
begin
  Writeln(MiddleValue);
end.
