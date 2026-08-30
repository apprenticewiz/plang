(*
Tier 3 capstone (integration): a real bug found WHILE WRITING this
capstone's own IOResult-under-{$I-} matrix, not merely suspected --
confirmed empirically against the local `fpc -Mtp` 3.2.2 install before
this file was written down.

Real Turbo Pascal/FPC treats a malformed numeric token read via
`read`/`readln` (Runtime error 106, "Invalid numeric format") as an
ORDINARY I/O failure subject to `{$I-}`/`{$I+}` exactly like every other
InOutRes code this tier's file model raises: under `{$I-}`, `fpc -Mtp`
sets IOResult to 106, assigns the destination variable 0, and lets the
program keep running -- verified directly:

    program t;
    {$mode tp}
    var i: Integer;
    begin
      i := 999;
      {$I-}
      read(i);
      writeln('ioresult=', IOResult);
      writeln('i=', i);
    end.

    $ echo "12abc" | ./t
    ioresult=106
    i=0

plang's own runtime does NOT do this. plang_read_i64_turbo/
plang_read_f64_turbo (runtime/plang_io.cpp) call plang_tp_runerror(106)
directly on a malformed token -- a `[[noreturn]]` reporter that
unconditionally aborts the process, the same way it correctly does for
`RunError`/the numbered range/overflow/nil-pointer checks (none of which
IS gated by `{$I-}` in real Turbo Pascal either -- IOChecks only ever
governs I/O operations, and `read` of a malformed token is Borland's own
one case where a "numeric parse" is ALSO an I/O operation). So under plang
`-std=turbo` today, this same program aborts with "Runtime error 106 at
$..." and exit status 106 regardless of whether `{$I-}` is in force --
`{$I-}`'s own graceful-degradation contract, otherwise honored end to end
by every other InOutRes code this capstone's matrix covers (see
ioresult-matrix-real-filesystem-driven-error-codes.pas), does not reach
this one path.

This is INTENTIONALLY not fixed here: this capstone's own scope is a test
corpus plus documentation, adding no new compiler features or runtime
behavior (see this PR's own description) -- and correcting this would mean
threading plang_read_i64_turbo/_f64_turbo through the same
RangeCheckGuards::ioChecksAt-gated `plang_iocheck()` checkpoint the file
model's own operations already use (see runtime/plang_file.cpp's
setInOutResIfClear and its callers), rather than the read call site
deciding to abort by itself -- a real CodeGen change, not a test-only one.
Filed here, pinned to plang's CURRENT (buggy) behavior so a future fix
shows up as an intentional, reviewed test change rather than a silent
regression, and cross-referenced from docs/turbo.md's own "Documented
deviations" section.

RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 106 %run %t < %S/Inputs/malformed-numeric-token.txt 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 106 at $
*)

var i: Integer;
begin
  i := 999;
  {$I-} { does NOT suppress the abort below -- see this file's own comment }
  read(i);
  writeln('unreachable: ioresult=', IOResult, ' i=', i);
end.
