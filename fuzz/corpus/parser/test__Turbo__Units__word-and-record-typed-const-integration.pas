(*
Turbo Tier 4 capstone (integration): item 3's own fix (Turbo Tier 4,
Cluster A item 3 -- a-units-record-and-array-typed-constant-round-trips-
through-the-tui.pas / a-units-sized-integer-shortstring-pchar-and-
procedural-vars-round-trip-through-the-tui.pas) proven at the same
larger, multi-unit integration scale as this capstone's own other tests,
not just that PR's own synthetic single-unit repro: GeoUnit exports a
RECORD-typed constant (Origin: TPoint) and a WORD-typed constant
(ScaleFactor), both consumed by name from an importer that ALSO links a
second, unrelated, separately-compiled unit (StatsUnit, its own Word-typed
constant and a Word-typed function) -- proving the .tui writer's constant
round-trip still holds once it is not the only unit in the link, and that
a Word-typed exported CONSTANT (not just a Word-typed exported VARIABLE,
which the sized-integer precedent test already covers) survives the same
-c/delete-source/link/run cycle.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/geounit.pas -o %t.dir/geounit.o
RUN: %plang -std=turbo -c %t.dir/statsunit.pas -o %t.dir/statsunit.o
RUN: FileCheck --check-prefix=TUI %s < %t.dir/geounit.tui
RUN: rm %t.dir/geounit.pas %t.dir/statsunit.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/geounit.o %t.dir/statsunit.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
TUI: const Origin: TPoint = (X: 0; Y: 0);
TUI: const ScaleFactor: Word = 100;
*)

(*
CHECK:origin=(0, 0)
CHECK-NEXT:scale-factor=100
CHECK-NEXT:scaled=(300, 700)
CHECK-NEXT:sample-count=500
CHECK-NEXT:bumped=501
*)

//--- geounit.pas
unit GeoUnit;

interface

type
  TPoint = record
    X, Y: Integer;
  end;

const
  Origin: TPoint = (X: 0; Y: 0);
  ScaleFactor: Word = 100;

procedure ScalePoint(P: TPoint; Factor: Word; var Result: TPoint);

implementation

procedure ScalePoint(P: TPoint; Factor: Word; var Result: TPoint);
begin
  Result.X := P.X * Factor;
  Result.Y := P.Y * Factor;
end;

end.

//--- statsunit.pas
unit StatsUnit;

interface

const
  SampleCount: Word = 500;

function Bump(W: Word): Word;

implementation

function Bump(W: Word): Word;
begin
  Bump := W + 1;
end;

end.

//--- main.pas
program GeoStatsIntegration;
uses GeoUnit, StatsUnit;
var
  P, Scaled: TPoint;
begin
  Writeln('origin=(', Origin.X, ', ', Origin.Y, ')');
  Writeln('scale-factor=', ScaleFactor);
  P.X := 3;
  P.Y := 7;
  ScalePoint(P, ScaleFactor, Scaled);
  Writeln('scaled=(', Scaled.X, ', ', Scaled.Y, ')');
  Writeln('sample-count=', SampleCount);
  Writeln('bumped=', Bump(SampleCount));
end.
