(*
Tier 3 capstone (integration): a second real bug found WHILE WRITING this
capstone's own combined Seek/Truncate scenario -- confirmed empirically
against the local `fpc -Mtp` 3.2.2 install before this file was written
down, the same way every other empirical claim in this tier's own tests
is.

FileMode defaults to 2 ("read-write"), and this is already pinned,
documented behavior (filemode-defaults-to-2-and-is-assignable.pas,
test/CodeGen/Turbo/, and docs/turbo.md's own upcoming file-model section).
Real Turbo Pascal/`fpc -Mtp` honors that default concretely: Reset opens
the underlying file read-write, so a Write (or a Seek+Truncate, its own
close cousin) against a file the program only ever Reset -- never
Rewrite/Append -- works, IOResult 0, exactly the "load a record, seek
back, patch it in place" idiom real TP field practice depends on:

    program t;
    {$mode tp}
    var f: file of Byte;
    begin
      assign(f, 't.bin'); rewrite(f); write(f, Byte(1)); write(f, Byte(2)); close(f);
      reset(f); seek(f, 0);
      {$I-} write(f, Byte(99)); writeln('ioresult=', IOResult); {$I+}
    end.

    $ ./t
    ioresult=0

plang's own plang_tp_reset (runtime/plang_file.cpp) always calls
`fopen(Name, "r")` -- read-only, unconditionally, regardless of FileMode's
own value -- so the identical program aborts instead, and NOT even
through Turbo's own InOutRes/{$I-} mechanism: the write call in this case
reaches the SHARED ISO/EP plang_err_* abort path ("plang runtime: write:
file is not open in the required mode", exit 70), the same one Turbo's
own file model exists specifically to route around for every OTHER
failure this tier's own IOResult matrix covers
(ioresult-matrix-real-filesystem-driven-error-codes.pas). `{$I-}` does
nothing at all here -- confirmed directly below.

This has a real, visible knock-on effect elsewhere in this very capstone:
seek-filepos-filesize-truncate-combined-scenario.pas cannot Seek+Truncate
a Reset-reopened file either (ftruncate on a read-only fd is genuinely
EINVAL at the OS level, independent of plang's own InOutRes plumbing), and
works around it by keeping the whole Seek/Truncate sequence inside the
ORIGINAL Rewrite-opened session instead, exactly like the
ALREADY-PASSING, pre-existing
truncate-shortens-a-file-at-the-current-position.pas (test/CodeGen/Turbo/)
already does -- which is very likely why that test was written that way
in the first place, and never against a freshly Reset file.

Not fixed here, for the same reason
read-of-malformed-numeric-input-does-not-yet-honor-i-minus-known-gap.pas
(right next to this file) isn't: this capstone's own scope is a test
corpus plus documentation, no new compiler/runtime behavior (see this
PR's own description). Fixing this for real means plang_tp_reset choosing
its fopen mode from FileMode ("r" vs "r+") and Write's own not-open-in-
the-required-mode check routing through Turbo's InOutRes contract instead
of the shared ISO/EP abort -- both genuine runtime changes, not test-only
ones. Pinned to plang's CURRENT (buggy) behavior so a future fix shows up
as an intentional, reviewed test change, and cross-referenced from
docs/turbo.md's own "Documented deviations" section.

RUN: %plang -std=turbo %s -o %t
RUN: not %run %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: plang runtime: write: file is not open in the required mode
*)

var f: file of Byte;
begin
  assign(f, 'reset-does-not-yet-open-read-write-known-gap.bin');
  rewrite(f);
  write(f, Byte(1));
  write(f, Byte(2));
  close(f);

  reset(f);
  seek(f, 0);
  {$I-} { does NOT suppress the abort below -- see this file's own comment }
  write(f, Byte(99));
  writeln('unreachable: ioresult=', IOResult);
  {$I+}
end.
