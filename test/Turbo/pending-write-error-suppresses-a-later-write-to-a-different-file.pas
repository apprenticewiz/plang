(*
Issue #738's own write-triggered repro, as a regression test: real Turbo
Pascal / `fpc -Mtp`'s checked-I/O-off contract is that once InOutRes is
pending (nonzero) and unread, EVERY subsequent I/O call -- on ANY file, not
just the one that failed -- becomes a silent no-op until IOResult is
explicitly called to clear it. Before this fix, plang only implemented that latch in
Eof/Eoln; every other Turbo I/O entry point (including Rewrite/Write/
Writeln against a file that has nothing at all to do with the one that
failed) proceeded normally.

The failing write is driven by genuine ENOSPC against /dev/full (the same
technique blockwrite-to-a-disk-full-device-reports-101-even-with-a-result-
argument.pas, test/CodeGen/Turbo/, already uses) -- a single small write can
land entirely in libc's own stdio buffer without ever reaching a real
write(2) syscall, so this writes enough data across enough Writeln calls to
force a real flush and a real failure, exactly like the issue's own repro.

Once InOutRes is left at 101 (pending, unread), a SECOND file -- 'good',
never touched by the failing write, freshly Assigned and Rewritten only
after the failure -- must not be opened, written, or closed at all:
confirmed against `fpc -Mtp`, the target path this test's own Rewrite would
otherwise create simply does not exist afterward (checked at the shell
level, not just via IOResult) -- the same proof
write-to-a-never-opened-file-is-a-silent-no-op-and-sets-ioresult-103.pas
(test/CodeGen/Turbo/) already gives for a file that was never opened to
begin with, extended here to a file whose OWN Rewrite call is itself
suppressed outright rather than merely never reached.

Finally, reading IOResult once clears the latch (confirmed to report the
original 101, not some later code) and lets a completely ordinary
Rewrite/Write/Close/Reset/Read round trip on a THIRD, freshly named file
succeed normally afterward -- the "resumes normal I/O" half of this
item's own requirement, not just "stays suppressed forever".

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
RUN: not test -f pending-write-error-suppresses-a-later-write-to-a-different-file-good.txt
*)

(*
REQUIRES: dev-full
*)

(*
CHECK:pending-write-code=101
CHECK-NEXT:resumed-ioresult=0 resumed-value=hello
*)

var
  bad, good, third: text;
  s: string;
  i: integer;
  code: integer;
begin
  {$I-}
  assign(bad, '/dev/full');
  rewrite(bad);
  s := '';
  for i := 1 to 200 do s := s + '0123456789';
  for i := 1 to 50 do writeln(bad, s); { forces a genuine ENOSPC: InOutRes=101, pending }

  { 'good' has nothing to do with 'bad' -- Assign/Rewrite/Writeln/Close here
    must all be silent no-ops while 101 sits pending and unread. }
  assign(good, 'pending-write-error-suppresses-a-later-write-to-a-different-file-good.txt');
  rewrite(good);
  writeln(good, 'this must never be written anywhere');
  close(good);

  code := IOResult; { clears the latch; reports the ORIGINAL 101, not 0 }
  writeln('pending-write-code=', code);

  { Normal I/O resumes once IOResult has been read: a fresh file, never
    touched by anything above, round-trips cleanly. }
  assign(third, 'pending-write-error-suppresses-a-later-write-to-a-different-file-third.txt');
  rewrite(third);
  writeln(third, 'hello');
  close(third);
  reset(third);
  readln(third, s);
  writeln('resumed-ioresult=', IOResult, ' resumed-value=', s);
end.
