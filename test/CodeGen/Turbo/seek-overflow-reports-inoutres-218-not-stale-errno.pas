(*
Issue #583: Seek(f, n) where n * RecSize overflows int64 used to report a
STALE, unrelated errno value as InOutRes instead of a well-defined error --
including IOResult 0 ("success") when nothing had failed recently, even
though the seek provably did not move the file position.  seekOffset's own
overflow-safe multiply (added for #403's SeekRead/SeekWrite/SeekUpdate)
returns false WITHOUT touching errno, so plang_tp_seek must not read errno
on that branch; it now reports the fixed EINVAL-equivalent InOutRes 218
directly, matching the negative-N case right next to this test (also 218,
but reached via a real fseek(3) failure) and matching `fpc -Mtp`.

An unrelated failed Reset on a second file first primes errno with ENOENT
(InOutRes 2), so a regression that goes back to reading stale errno on the
overflow path would report 2 here instead of 218.

CI-caught macOS portability fix (post-#583, PR #759): this test originally
used `file of Byte` (RecSize 1) with n = 9223372036854775807 (INT64_MAX).
n * 1 does NOT overflow int64 -- it's exactly INT64_MAX, still perfectly
representable -- so seekOffset() actually returned TRUE and the real
fseek(3)/SEEK_SET call underneath was reached with that huge-but-valid
offset. On Linux/ext4 that fseek happens to fail (EINVAL, InOutRes 218,
apparently because ext4's max file size is well under 2^63-1), which
accidentally produced the "right" answer -- but that is real OS+filesystem
lseek(2) behavior, not the #583 overflow-detection code path this test
claims to cover, and it is exactly the kind of thing that legitimately
differs by platform/filesystem (macOS CI showed a different outcome here).
That fseek fallback is not itself a bug: for an offset that provably fits
in int64, whether the OS accepts a seek that far out is inherently a real,
platform-dependent capability question, not something plang's own
arithmetic can or should decide.

To actually exercise seekOffset()'s overflow check -- deterministic,
portable __builtin_mul_overflow arithmetic computed entirely in plang's own
code before fseek is ever called, so it cannot depend on any OS's lseek/
errno behavior -- this test now uses `file of Int64` (RecSize 8) with the
same n = INT64_MAX: n * 8 overflows int64 by construction on every
platform, so seekOffset() returns false and plang_tp_seek reports 218
directly, without ever reaching fseek(3) at all.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:2
CHECK-NEXT:218
CHECK-NEXT:0
*)

var
  f: file of Int64;
  g: Text;
begin
  {$I-}
  assign(g, 'seek-overflow-reports-inoutres-218-not-stale-errno-missing.txt');
  reset(g);
  writeln(IOResult);

  assign(f, 'seek-overflow-reports-inoutres-218-not-stale-errno.bin');
  rewrite(f);
  write(f, Int64(1));
  close(f);
  assign(f, 'seek-overflow-reports-inoutres-218-not-stale-errno.bin');
  reset(f);

  seek(f, 9223372036854775807);
  writeln(IOResult);
  writeln(FilePos(f));
  {$I+}
  close(f);
end.
