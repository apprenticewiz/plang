(*
An integration gap none of the sibling switch or include tests cover alone:
the position-keyed SwitchTable (Basic/SwitchTable.h) has its own comment
describing a real, historical bug at exactly the seam this file exercises --
an early shape of SwitchTable::record silently discarded the include's own
last recorded point when the scanner resumed the includer afterwards
(popInclude's own resume-point record has a *smaller* raw offset than
anything recorded inside the include, since SourceManager lays buffers out
in the order they are opened, not the order their text is read), corrupting
"every query for a location later in the include than its own last switch
directive". The switch tests (switch-directive-r-plus/-minus-...) never
open a second file, and the include tests (nested-includes-...,
fi-search-path-...) never touch a switch, so neither alone exercises the
boundary the bug was actually in.

Two scenarios, both proving {$R+} set INSIDE an included file is seen by a
range check with RangeChecks off by turbo's own default (no -frange-checks,
no other {$R} anywhere):

  - "within": the array write the switch is meant to guard is itself inside
    the SAME include, after the include's own {$R+} -- the direct case the
    SwitchTable.h comment describes ("a location later in the include than
    its own last switch directive").
  - "after": the include contains only the directive; the guarded write is
    back in the includer, after the {$I} line -- proving popInclude's
    resume-point record actually carries the state across, not just that
    the include's own internal state was self-consistent.

Both are reads set up by an earlier unchecked write (the same
"read, not a write, for the observably-unchecked side" reasoning the sibling
{$R+}/{$R-} switch tests use -- an out-of-range WRITE with checking off
genuinely scribbles on whatever memory the index lands in, not something a
lit test should depend on).

"after"'s own unchecked write is wrapped in a record with a large trailing
Buf field rather than a bare top-level array: record fields keep their
declared relative order (unlike separate top-level locals, which a compiler
may lay out however it likes), so the scribble is guaranteed to land in Buf
-- still that same record's own storage -- rather than an unrelated local or
a spilled register a later, CHECKED write's own error-reporting call depends
on.  Confirmed necessary, not defensive: a bare `array[1..3] of integer`
here let codegen changes elsewhere in the compiler perturb the stack layout
enough that this exact scribble started corrupting the SECOND write's own
runtime-error CODE argument under `-O2`/`-O3` (passing, misleadingly, under
an unoptimized build) -- a real, reproduced failure, not a hypothetical one.
*)

(*
RUN: split-file %s %t.dir

RUN: %plang -std=turbo %t.dir/within/main.pas -o %t.dir/within.bin
RUN: %checkexit 201 %run %t.dir/within.bin > %t.dir/within.out 2> %t.dir/within.err
RUN: FileCheck --check-prefix=WITHIN-OUT %s < %t.dir/within.out
RUN: FileCheck --check-prefix=RTE201 %s < %t.dir/within.err

RUN: %plang -std=turbo %t.dir/after/main.pas -o %t.dir/after.bin
RUN: %checkexit 201 %run %t.dir/after.bin > %t.dir/after.out 2> %t.dir/after.err
RUN: FileCheck --check-prefix=AFTER-OUT %s < %t.dir/after.out
RUN: FileCheck --check-prefix=RTE201 %s < %t.dir/after.err
*)

(*
WITHIN-OUT:before include
AFTER-OUT:before include: silently out of range, still running
RTE201: Runtime error 201 at $
*)

//--- within/main.pas
program within_main;
var a: array[1..3] of integer;
    i: integer;
begin
  i := 10;
  writeln('before include');
  {$I turnon.inc}
  writeln('unreachable: within-include write did not abort')
end.

//--- within/turnon.inc
  {$R+}
  a[i] := 1;
  writeln('unreachable: still inside include after {$R+}');

//--- after/main.pas
program after_main;
type Frame = record
       a: array[1..3] of integer;
       Buf: array[1..256] of integer;
     end;
var f: Frame;
    i: integer;
begin
  i := 10;
  f.a[i] := 1;
  writeln('before include: silently out of range, still running');
  {$I turnon.inc}
  f.a[i] := 2;
  writeln('unreachable: after-include write did not abort')
end.

//--- after/turnon.inc
  {$R+}
