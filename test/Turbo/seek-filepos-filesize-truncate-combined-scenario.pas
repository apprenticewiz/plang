(*
Tier 3 capstone (integration): Seek/FilePos/FileSize/Truncate composed in
one realistic scenario, past the pairwise unit proofs
(seek-filepos-and-filesize-agree-on-a-real-file.pas,
truncate-shortens-a-file-at-the-current-position.pas, both
test/CodeGen/Turbo/) -- write several records, seek partway in, Truncate
there, confirm FileSize reflects the truncation, then Seek BACK toward the
start and confirm the surviving records are still exactly what was
written, not corrupted by the truncation next to them.

Seek/Truncate/FileSize all run in the SAME Rewrite-opened session, not
after a Reset -- confirmed while writing this test, not assumed:
plang_tp_reset (runtime/plang_file.cpp) used to always fopen() "r"
(read-only), never honoring FileMode's own documented read-write default of
2 (filemode-defaults-to-2-and-is-assignable.pas), so Seek+Truncate against
a Reset-reopened file used to fail with a genuine EINVAL/218 even though
the identical program succeeds against a real `fpc -Mtp` (which does open
Reset read-write) -- see reset-opens-read-write.pas (test/Turbo/) for that
gap, now fixed, pinned and explained on its own; and
seek-truncate-after-fresh-reset.pas, right next to this file, for the
Reset-session Seek+Truncate path this test's own Rewrite-session shape
could not cover before the fix. This test's own shape predates the fix and
is left as it was -- matching the ALREADY-PASSING shape
truncate-shortens-a-file-at-the-current-position.pas already uses:
Seek/Truncate happen before the file is ever closed, and only the FINAL
read-back goes through a fresh Reset (read-only is all a pure read needs).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
RUN: wc -c < seek-filepos-filesize-truncate-combined-scenario.bin | tr -d ' ' | FileCheck --check-prefix=SIZE %s
*)

(*
CHECK:FileSize before truncate=10
CHECK-NEXT:FilePos after seek(4)=4
CHECK-NEXT:FileSize after truncate=4
CHECK-NEXT:record 0=10
CHECK-NEXT:record 1=20
CHECK-NEXT:record 2=30
CHECK-NEXT:record 3=40
CHECK-NEXT:FilePos after reading all 4 surviving records=4
SIZE:4
*)

var
  f: file of Byte;
  i: Byte;
  v: Byte;
begin
  assign(f, 'seek-filepos-filesize-truncate-combined-scenario.bin');
  rewrite(f);
  for i := 1 to 10 do write(f, Byte(i * 10));
  writeln('FileSize before truncate=', filesize(f));

  seek(f, 4);
  writeln('FilePos after seek(4)=', filepos(f));
  truncate(f);
  writeln('FileSize after truncate=', filesize(f));
  close(f);

  reset(f);
  seek(f, 0);
  for i := 0 to 3 do begin
    read(f, v);
    writeln('record ', i, '=', v);
  end;
  writeln('FilePos after reading all 4 surviving records=', filepos(f));
  close(f);
end.
