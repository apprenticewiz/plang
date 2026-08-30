(*
Tier 3 gap fix (was: reset-does-not-yet-open-read-write-known-gap.pas): a
second real bug found WHILE WRITING the Tier 3 capstone's own combined
Seek/Truncate scenario, confirmed empirically against the local
`fpc -Mtp` 3.2.2 install, and now fixed.

FileMode defaults to 2 ("read-write"), and this is already pinned,
documented behavior (filemode-defaults-to-2-and-is-assignable.pas,
test/CodeGen/Turbo/, and docs/turbo.md's own file-model section). Real
Turbo Pascal/`fpc -Mtp` honors that default concretely: Reset opens the
underlying file read-write, so a Write (or a Seek+Truncate, its own close
cousin) against a file the program only ever Reset -- never
Rewrite/Append -- works, IOResult 0, exactly the "load a record, seek back,
patch it in place" idiom real TP field practice depends on:

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

plang's own plang_tp_reset (runtime/plang_file.cpp) used to always call
`fopen(Name, "r")` -- read-only, unconditionally, regardless of FileMode's
own value -- so the identical program aborted instead, and not even
through Turbo's own InOutRes/{$I-} mechanism: the write call reached the
SHARED ISO/EP plang_err_file_wrong_mode abort path ("plang runtime: write:
file is not open in the required mode", exit 70).

Fixed in two parts:

1. plang_tp_reset now chooses its open mode from the CURRENT value of
   FileMode -- confirmed against `fpc -Mtp` (docs in plang_tp_reset's own
   comment): FileMode 0 (read-only) opens "r" exactly as before; FileMode 2
   (read-write, the default) opens "r+"; FileMode 1 (write-only) opens via
   a raw POSIX open(O_WRONLY) (no fopen(3) mode string exists for
   "write-only, do not truncate, fail if absent", and Reset must never
   truncate, unlike Rewrite).

2. The `_turbo`-suffixed write/read entry points' own wrong-direction/
   wrong-mode checks (trapOnWrongDirection/trapOnStreamError,
   runtime/plang_file.cpp) now route Turbo failures through a Turbo-only
   InOutRes twin (tpTrapOnWrongDirection/tpTrapOnStreamError, following
   this tier's own tpFileReady naming convention) instead of the shared,
   ISO/EP-reachable plang_err_file_wrong_mode abort -- so a genuine
   direction violation (e.g. Write against a FileMode-0 Reset) now sets
   InOutRes 105 ("file not open for output") under {$I-} rather than
   aborting, while ISO/EP's own plang_err_file_wrong_mode is completely
   untouched and still aborts unconditionally, as it must.

This has a real, visible knock-on effect elsewhere in this tier's own
capstone: seek-filepos-filesize-truncate-combined-scenario.pas could not
Seek+Truncate a Reset-reopened file before this fix (ftruncate on a
read-only fd is genuinely EINVAL at the OS level, independent of plang's own
InOutRes plumbing) and worked around it by keeping the whole Seek/Truncate
sequence inside the ORIGINAL Rewrite-opened session instead;
seek-truncate-after-fresh-reset.pas (test/Turbo/) is this fix's own sibling
covering the now-working Reset-session path directly.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK: ioresult=0
*)

var f: file of Byte;
begin
  assign(f, 'reset-opens-read-write.bin');
  rewrite(f);
  write(f, Byte(1));
  write(f, Byte(2));
  close(f);

  reset(f);
  seek(f, 0);
  {$I-} write(f, Byte(99)); writeln('ioresult=', IOResult); {$I+}
  close(f);
end.
