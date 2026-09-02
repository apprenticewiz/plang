(*
BlockWrite's own arity-dependent short-transfer behavior -- the write-side
twin of blockread-without-result-argument-a-short-read-sets-inoutres-100.pas,
right next to this test.  Without a result argument, a short (here: totally
failed -- the file is reopened read-only, so every byte of the attempted
write is refused at the C stdio level) write IS an error.

Issue #665 correction: this test used to check for InOutRes 101 ("disk
write error"), on the theory that a short write without a result argument
always means 101 the way a short read without one always means 100. That
theory was wrong for BlockWrite specifically -- re-confirmed against
`fpc -Mtp` while fixing #665: a write refused because the file itself is
open in the wrong direction (exactly this test's own read-only-Reset setup)
reports 105 ("file not open for output"), the same direction-violation code
every other Turbo write entry point in this file reports, not the generic
101 a genuine OS-level failure (e.g. ENOSPC) gets instead -- see
blockwrite-to-a-disk-full-device-reports-101-even-with-a-result-argument.pas
for that other, genuinely-101 case, right next to this one.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:105
*)

var
  f: file;
  buf: array[0..9] of Byte;
  i: Integer;
begin
  assign(f, 'blockwrite-without-result-argument-a-direction-violation-sets-inoutres-105.bin');
  rewrite(f, 1);
  for i := 0 to 9 do buf[i] := i;
  blockwrite(f, buf, 5);
  close(f);

  { FileMode forced to 0 (read-only): Tier 3's own gap fix
    (test/Turbo/reset-opens-read-write.pas) now has Reset honor FileMode's
    documented read-write default of 2, so without this, the BlockWrite
    below would genuinely succeed instead of being refused. }
  FileMode := 0;
  reset(f, 1); (* read-only: every byte of a following write is refused *)
  {$I-}
  blockwrite(f, buf, 5);
  writeln(IOResult);
  {$I+}
  close(f);
end.
