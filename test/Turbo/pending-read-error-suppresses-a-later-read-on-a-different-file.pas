(*
Issue #738's own read-triggered repro (the "also confirmed with a Read-based
variant" paragraph of the issue's own write-up), as a regression test: once
InOutRes is pending (nonzero) and unread, a later Read against a completely
different, perfectly healthy file must be a silent no-op too -- not just a
later Write, and not just the same file that failed.

The failing read is a typed file at EOF (issue #661's own mechanism,
confirmed elsewhere in this suite to set InOutRes 100 even under default
checked I/O): a `file of Integer` is Reset immediately after being freshly
Rewritten with nothing written to it, so the very first Read is already
past end of file.

'good', reset and primed at its first line BEFORE 'bad' ever fails, is
never touched by the failure itself -- proving the suppression is a GLOBAL
InOutRes latch, not something scoped to whichever file most recently set
it. Once suppressed, confirmed against `fpc -Mtp`:
  - the destination string is left EMPTY, not merely unchanged (a plang-
    and-fpc-shared quirk of how a Read into a string variable is
    implemented -- it clears the destination before it ever gets far enough
    to check whether anything can actually be read -- confirmed to NOT hold
    for BlockRead's raw buffer or a typed-record Read's destination
    elsewhere in this suite, which really do leave their own destination
    completely untouched instead)
  - 'good' itself is not advanced at all: a plain Readln against it AFTER
    the latch clears still reads the file's FIRST line, proving the
    suppressed Readln truly performed no I/O whatsoever, not merely
    "failed after consuming something."

Finally, reading IOResult once clears the latch (reporting the ORIGINAL
100, not some later code) and lets a completely ordinary Readln against
'good' succeed normally afterward.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:pending-read-code=100 suppressed-s=[]
CHECK-NEXT:resumed-ioresult=0 resumed-value=[first line]
*)

var
  bad: file of integer;
  good: text;
  x: integer;
  s: string;
  code: integer;
begin
  {$I-}
  assign(good, 'pending-read-error-suppresses-a-later-read-on-a-different-file-good.txt');
  rewrite(good);
  writeln(good, 'first line');
  writeln(good, 'second line');
  close(good);
  reset(good); { open, healthy, positioned at the very start }

  assign(bad, 'pending-read-error-suppresses-a-later-read-on-a-different-file-bad.dat');
  rewrite(bad);
  reset(bad);
  read(bad, x); { nothing was ever written: past EOF, InOutRes=100, pending }

  s := 'this must not survive';
  readln(good, s); { must be a total no-op: no I/O against 'good' at all }
  code := IOResult; { clears the latch; reports the ORIGINAL 100, not 0 }
  writeln('pending-read-code=', code, ' suppressed-s=[', s, ']');

  { Normal I/O resumes once IOResult has been read: 'good' was never
    actually advanced by the suppressed readln above, so this reads its
    FIRST line, not its second. }
  readln(good, s);
  writeln('resumed-ioresult=', IOResult, ' resumed-value=[', s, ']');
end.
