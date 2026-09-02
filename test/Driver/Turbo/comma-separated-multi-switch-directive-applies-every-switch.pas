(*
Issue #658: real Turbo/FPC let several one-letter switches share one
directive comment via a comma, `{$R+,I-}` -- each comma-separated element
its own adjacent Letter+Sign pair, applied as if it had been written as
that many separate single-switch directives.  Before this fix, the whole
body ("R+,I-") was handed to dispatchSwitchDirective as one Name/Argument
pair ("R", "+,I-"); "+,I-" is not "+" or "-", so nothing was applied AND
the fallback path reported "unknown compiler directive 'R'" -- misleading,
since R (RangeChecks) is a perfectly well-known switch on its own, just
not spelled this way.

Proof: {$R+,I-} turns RangeChecks on (an out-of-range read then traps,
RTE 201 -- the same read-not-write proof
switch-directive-r-plus-turns-range-checks-on-partway-through-the-file.pas
uses) AND compiles with no "unknown compiler directive" diagnostic at all.
*)

(*
RUN: %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=NOWARN --allow-empty %s < %t.err
RUN: %checkexit 201 %run %t > %t.out
RUN: FileCheck %s < %t.out
*)

(*
NOWARN-NOT: unknown compiler directive
CHECK:before the comma-form directive
*)

program comma_multi_switch;
var a: array[1..3] of integer;
    i, dummy: integer;
begin
  i := 10;
  {$R-}
  writeln('before the comma-form directive');
  {$R+,I-}
  dummy := a[i];
  writeln('unreachable: {$R+,I-} did not enable range checks')
end.
