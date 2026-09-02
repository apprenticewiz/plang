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

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:2
CHECK-NEXT:218
CHECK-NEXT:0
*)

var
  f: file of Byte;
  g: Text;
begin
  {$I-}
  assign(g, 'seek-overflow-reports-inoutres-218-not-stale-errno-missing.txt');
  reset(g);
  writeln(IOResult);

  assign(f, 'seek-overflow-reports-inoutres-218-not-stale-errno.bin');
  rewrite(f);
  write(f, Byte(1));
  close(f);
  assign(f, 'seek-overflow-reports-inoutres-218-not-stale-errno.bin');
  reset(f);

  seek(f, 9223372036854775807);
  writeln(IOResult);
  writeln(FilePos(f));
  {$I+}
  close(f);
end.
