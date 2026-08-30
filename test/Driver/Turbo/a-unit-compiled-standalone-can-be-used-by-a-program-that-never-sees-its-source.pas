(*
Turbo Tier 4, Cluster A item 2's own central claim: real separate
compilation, not item 1's own temporary loader (which re-parsed
"<name>.pas" from scratch on every 'uses', so it never actually needed the
unit to have been compiled first).

This test proves it the strongest way available: compile MathUnit.pas on
its own (`plang -c`), publish its .tui and its .o, then DELETE the .pas
source entirely and compile+link a program against nothing but the .tui and
the .o -- if the importer's own compile still succeeds and the program still
runs correctly, nothing downstream of the first `plang -c` ever needed the
unit's own source text again.  It exercises every kind of Turbo interface
declaration this item wires real cross-object codegen for: a folded
constant (Greeting), a variable with real storage in the unit's own object
(Counter), and a function AND a procedure, both called across the object
boundary (Square, Bump) -- see this item's own report for exactly what is
and is not covered.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/mathunit.pas -o %t.dir/mathunit.o
RUN: test -e %t.dir/mathunit.tui
RUN: rm %t.dir/mathunit.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/mathunit.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:25
CHECK-NEXT:11
CHECK-NEXT:7
*)

//--- mathunit.pas
unit MathUnit;

interface

const Greeting = 7;
var Counter: Integer;

function Square(x: Integer): Integer;
procedure Bump;

implementation

function Square(x: Integer): Integer;
begin
  Square := x * x;
end;

procedure Bump;
begin
  Counter := Counter + 1;
end;

end.

//--- main.pas
program UsesMathUnit;
uses MathUnit;
begin
  Counter := 10;
  Bump;
  Writeln(Square(5));
  Writeln(Counter);
  Writeln(Greeting);
end.
