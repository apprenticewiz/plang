(*
Turbo Tier 4, Cluster C item 6: Dos.DiskFree/DiskSize are statvfs(2)
wrappers over the current working directory's own real filesystem
(runtime/plang_dos.cpp's own plang_dos_diskfree/plang_dos_disksize) --
Drive is a real DOS drive-letter index on real Borland/FPC, meaningless on
POSIX; this implementation's own field-practice-confirmed reinterpretation
(Dos.pas's own header comment, matching real FPC's own rtl/unix/dos.pp)
is that Drive=0, and every other value, means "the current directory".
Cannot pin an exact byte count (real, host-dependent free space), so this
checks the real, structural properties that must hold: both values are
positive, DiskSize is at least DiskFree (a filesystem cannot have more
free space than its own total size), and Drive=0 and Drive=3 agree on the
filesystem's own TOTAL size (DiskSize, not DiskFree -- free space can
genuinely fluctuate between two real statvfs(2) calls even microseconds
apart, which would make an exact-equality check on it a source of real,
if rare, flakiness; total size does not) -- no real per-drive registry
exists in this implementation, see Dos.pas's own header comment on why
that is a deliberate, documented scope cut.

RUN: %plang -std=turbo -I%plang_unit_dir %s -o %t
RUN: %run %t | FileCheck %s
*)

program DosDiskFreeDiskSize;
uses Dos;
var
  Free0, Size0, Size3: Int64;
begin
  Free0 := DiskFree(0);
  Size0 := DiskSize(0);
  Size3 := DiskSize(3);
  Writeln('free-positive: ', Free0 > 0);
  Writeln('size-positive: ', Size0 > 0);
  Writeln('size-at-least-free: ', Size0 >= Free0);
  Writeln('drive-reinterpreted-uniformly: ', Size0 = Size3);
end.

(*
CHECK:free-positive: TRUE
CHECK-NEXT:size-positive: TRUE
CHECK-NEXT:size-at-least-free: TRUE
CHECK-NEXT:drive-reinterpreted-uniformly: TRUE
*)
