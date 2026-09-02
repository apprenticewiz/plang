(*
Issue #604: real Turbo/FPC tell the RangeChecks switch (`{$R+}`, the sign
immediately adjacent to the letter, no gap) apart from an unrelated named
directive that happens to share the very same letter (`{$R resourcefile}`,
a Windows-resource directive plang does not implement -- `{$R +}`, with a
gap, parses the same way: Name "R", Argument "+" with a space before it,
which is not the switch spelling at all) purely by whether the sign is
adjacent -- confirmed against `fpc -Mtp`, which opens `+.res` for `{$R +}`
rather than touching range checking.  Before this fix, splitDirectiveBody
trimmed the space away before dispatchSwitchDirective ever saw it, so
`{$R +}` silently enabled range checking exactly like `{$R+}` -- whitespace
was changing what the directive MEANT, not just how it was spelled.

Two programs, identical but for the one space: `{$R+}` must abort with a
real range-check trap (RTE 201, the same proof
switch-directive-r-plus-turns-range-checks-on-partway-through-the-file.pas
uses); `{$R +}` must NOT -- plang does not implement the resource
directive, so this should read as plang's own honest "never heard of
this", not as a switch that silently changed compiler state under a
different spelling.  A READ, not a write, for the out-of-range access in
the "spaced" program, same reasoning as that sibling test's own comment:
an out-of-range WRITE with checking off genuinely scribbles on whatever
memory the index lands in, which is not something a lit test can depend on
landing anywhere in particular, let alone still running afterward to print
its own proof line.
*)

(*
RUN: split-file %s %t.dir

RUN: %plang -std=turbo %t.dir/adjacent.pas -o %t.dir/adjacent.bin
RUN: %checkexit 201 %run %t.dir/adjacent.bin

RUN: %plang -std=turbo %t.dir/spaced.pas -o %t.dir/spaced.bin 2> %t.dir/spaced.err
RUN: FileCheck --check-prefix=UNKNOWN %s < %t.dir/spaced.err
RUN: %run %t.dir/spaced.bin | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s
*)

(*
UNKNOWN: unknown compiler directive 'R'
RAN:not trapped: {$R +} did not enable range checks
*)

//--- adjacent.pas
program adjacent;
var a: array[1..3] of integer;
    i: integer;
begin
  i := 10;
  {$R+}
  a[i] := 1;
  writeln('unreachable: {$R+} did not trap')
end.

//--- spaced.pas
program spaced;
var a: array[1..3] of integer;
    i, dummy: integer;
begin
  i := 10;
  {$R +}
  dummy := a[i];
  writeln('not trapped: {$R +} did not enable range checks')
end.
