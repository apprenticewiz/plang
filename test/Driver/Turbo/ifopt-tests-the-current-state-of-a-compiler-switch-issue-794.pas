(*
{$IFOPT switch+}/{$IFOPT switch-} (issue #794) tests the CURRENT state of a
compiler switch at the point it appears -- exactly what
Scanner::CurrentSwitchState (or, before any switch directive has run,
Opts.defaultSwitches()) already tracks for every switch in
CompilerSwitches.def, live or merely recorded.  Four independent programs via
split-file:
  - positive.pas: {$R+} turns RangeChecks on, then {$IFOPT R+} matches --
    the branch that wants it ON is the one that runs.
  - negative.pas: {$R-} turns it back off, then the SAME {$IFOPT R+} takes
    the {$ELSE} branch instead -- proving this is a live query of the
    CURRENT state, not just "R was mentioned somewhere".
  - nested.pas: {$IFOPT}/{$ELSE}/{$ENDIF} shares the exact same CondStack/
    skipToNextConditionalMarker machinery {$IFDEF}/{$IFNDEF} already use, so
    an {$IFDEF} nested inside a live {$IFOPT} branch (and vice versa) has to
    nest correctly rather than one confusing the other's {$ENDIF} for its
    own.
  - default.pas: no switch directive has run at all when {$IFOPT} is
    reached, so CurrentSwitchState was never built and the query has to fall
    back to Opts.defaultSwitches() -- -std=turbo's own default has
    RangeChecks off (CompilerSwitches.def's TurboDefault column), so this
    must take the "off" branch with nothing else in the file to have set it.
*)

(*
RUN: split-file %s %t.dir

RUN: %plang -std=turbo %t.dir/positive.pas -o %t.dir/positive.bin
RUN: %run %t.dir/positive.bin | FileCheck --check-prefix=POSITIVE --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo %t.dir/negative.pas -o %t.dir/negative.bin
RUN: %run %t.dir/negative.bin | FileCheck --check-prefix=NEGATIVE --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo %t.dir/nested.pas -o %t.dir/nested.bin
RUN: %run %t.dir/nested.bin | FileCheck --check-prefix=NESTED --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo %t.dir/default.pas -o %t.dir/default.bin
RUN: %run %t.dir/default.bin | FileCheck --check-prefix=DEFAULT --strict-whitespace --match-full-lines %s
*)

(*
POSITIVE:range checks on
NEGATIVE:range checks off
NESTED:outer-on
NESTED-NEXT:inner-defined
NESTED-NEXT:done
DEFAULT:range checks off (default)
*)

//--- positive.pas
{$R+}
program IfoptPositive;
begin
  {$IFOPT R+}
  writeln('range checks on');
  {$ELSE}
  writeln('range checks off');
  {$ENDIF}
end.

//--- negative.pas
{$R+}
{$R-}
program IfoptNegative;
begin
  {$IFOPT R+}
  writeln('range checks on');
  {$ELSE}
  writeln('range checks off');
  {$ENDIF}
end.

//--- nested.pas
{$R+}
{$DEFINE FEATURE}
program IfoptNested;
begin
  {$IFOPT R+}
  writeln('outer-on');
  {$IFDEF FEATURE}
  writeln('inner-defined');
  {$ELSE}
  writeln('inner-undefined');
  {$ENDIF}
  {$ELSE}
  writeln('outer-off');
  {$ENDIF}
  writeln('done')
end.

//--- default.pas
program IfoptDefault;
begin
  {$IFOPT R+}
  writeln('range checks on (default)');
  {$ELSE}
  writeln('range checks off (default)');
  {$ENDIF}
end.
