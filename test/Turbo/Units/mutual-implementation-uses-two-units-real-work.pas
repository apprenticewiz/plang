(*
Turbo Tier 4 capstone (integration): mutual implementation-`uses` between
two real, separately-compiled units, in a more elaborate scenario than
two-units-mutual-implementation-uses-compiles-and-links-real-fpc-
confirmed.pas's own minimal proof (that test's own PA/PB do nothing but
Writeln a fixed string each -- enough to prove the LINK graph works, not
that real DATA flows both directions through it).

Here both units do genuine work and both directions of the mutual
dependency actually get exercised at runtime, not just compiled:
UnitValidator.IsValidScore validates a range and, on an invalid score,
calls INTO UnitLogger.LogWarning to record what it saw; UnitLogger.
FormatScore calls INTO UnitValidator.IsValidScore to decide whether to
wrap a score as "[INVALID]" before printing it.  Compiled as two
completely separate `plang -c` invocations (neither unit's compile has
ever seen the OTHER unit compiled first -- UnitValidator only needs
UnitLogger's own already-parsed INTERFACE, and vice versa, the same
no-real-circular-type-dependency reasoning the precedent test's own header
comment explains), then linked with a program that calls into both.

main computes all three results into locals BEFORE printing anything, so
the CHECK order below is deterministic: both LogWarning calls (raised
while computing F2 and V) land before any of the summary Writelns that
read the now-fully-computed locals, rather than interleaving mid-line with
whichever Writeln happened to trigger them.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -I%t.dir -c %t.dir/unitvalidator.pas -o %t.dir/unitvalidator.o
RUN: %plang -std=turbo -I%t.dir -c %t.dir/unitlogger.pas -o %t.dir/unitlogger.o
RUN: rm %t.dir/unitvalidator.pas %t.dir/unitlogger.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/unitvalidator.o %t.dir/unitlogger.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:warning: out-of-range score seen: 140
CHECK-NEXT:warning: out-of-range score seen: -1
CHECK-NEXT:formatted 85 = [85]
CHECK-NEXT:formatted 140 = [INVALID:140]
CHECK-NEXT:direct IsValidScore(-1)=[FALSE]
*)

//--- unitvalidator.pas
unit UnitValidator;

interface

function IsValidScore(Score: Integer): Boolean;

implementation

uses UnitLogger;

function IsValidScore(Score: Integer): Boolean;
var
  Tail: string;
  OK: Boolean;
begin
  OK := (Score >= 0) and (Score <= 100);
  IsValidScore := OK;
  if not OK then
  begin
    Str(Score, Tail);
    LogWarning('out-of-range score seen: ' + Tail);
  end;
end;

end.

//--- unitlogger.pas
unit UnitLogger;

interface

procedure LogWarning(const Msg: string);
function FormatScore(Score: Integer): string;

implementation

uses UnitValidator;

procedure LogWarning(const Msg: string);
begin
  Writeln('warning: ', Msg);
end;

function FormatScore(Score: Integer): string;
var
  Tail: string;
begin
  Str(Score, Tail);
  if IsValidScore(Score) then
    FormatScore := Tail
  else
    FormatScore := 'INVALID:' + Tail;
end;

end.

//--- main.pas
program MutualRealWork;
uses UnitValidator, UnitLogger;
var
  F1, F2: string;
  V: Boolean;
begin
  F1 := FormatScore(85);
  F2 := FormatScore(140);
  V := IsValidScore(-1);
  Writeln('formatted 85 = [', F1, ']');
  Writeln('formatted 140 = [', F2, ']');
  Writeln('direct IsValidScore(-1)=[', V, ']');
end.
