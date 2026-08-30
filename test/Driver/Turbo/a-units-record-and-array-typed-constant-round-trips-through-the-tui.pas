(*
Turbo Tier 4, Cluster A item 3: a Turbo typed constant whose value is a
structured literal (`const CRec: TRec = (X: 1; Y: 2);`, parsed by
parseTurboConstValue as a StructuredValueExpr with an EMPTY TypeName --
Turbo's own '(...)' syntax never repeats the type name the const's own
'identifier : type' already gave it, unlike EP's structured-value
constructor 'TypeName[arm...]', which always sets one) used to be written
back by the SAME exprToString case EP's constructor uses, unconditionally:
'TypeName + "["..."]"'. With an empty TypeName that produced a bare
'[X: 1; Y: 2]', valid nowhere -- not even the same grammar the value came
from, whose own reader expects '(' -- so the .tui file's reparse failed
outright ("expected ']', got ':'"), taking down every OTHER declaration in
the same unit's interface with it, not just the structured constant.

A second, narrower bug sat right next to it: exprToString unconditionally
wrote ': ' for a structured-value arm's label position even when the arm
had no label at all (a plain positional array element, Arm.Labels empty),
turning 'const Arr1: TArr = (10, 20, 30);' into '(: 10, : 20, : 30)' --
also unparseable, and also unrelated to the '(' vs '[' bracket fix.

This test proves both are fixed together, and that the fix survives a real
-c/link/run cycle after the source is gone: a record-typed constant (CRec),
an array-typed constant (Arr1, purely positional -- the second bug's own
repro shape), and a record containing a NESTED array-typed field
initializer (Nested, which recurses through the same code twice).

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/structconstunit.pas -o %t.dir/structconstunit.o
RUN: FileCheck --check-prefix=TUI %s < %t.dir/structconstunit.tui
RUN: rm %t.dir/structconstunit.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/structconstunit.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
TUI: const CRec: TRec = (X: 1; Y: 2);
TUI: const Arr1: TArr = (10, 20, 30);
TUI: const Nested: TRec2 = (A: 5; B: (1, 2, 3));
*)

(*
CHECK:1 2
CHECK-NEXT:10 20 30
CHECK-NEXT:5 1 2 3
*)

//--- structconstunit.pas
unit StructConstUnit;

interface

type
  TRec  = record
    X, Y: Integer;
  end;
  TArr  = array[1..3] of Integer;
  TRec2 = record
    A: Integer;
    B: TArr;
  end;

const
  CRec:   TRec  = (X: 1; Y: 2);
  Arr1:   TArr  = (10, 20, 30);
  Nested: TRec2 = (A: 5; B: (1, 2, 3));

implementation

end.

//--- main.pas
program UsesStructConstUnit;
uses StructConstUnit;
begin
  Writeln(CRec.X, ' ', CRec.Y);
  Writeln(Arr1[1], ' ', Arr1[2], ' ', Arr1[3]);
  Writeln(Nested.A, ' ', Nested.B[1], ' ', Nested.B[2], ' ', Nested.B[3]);
end.
