(*
Issue #666: Eof(f)'s one-character lookahead window (F->Buf) survived
Truncate, BlockWrite, and a typed Write unchanged, even though each of
those genuinely moves/mutates the file out from under whatever byte the
window had cached -- so a following Eof answered from a byte that, after
the operation, was no longer the file's next unread byte (or did not exist
in the file at all any more). plang_tp_seek and plang_tp_blockread already
reset the window on their own position-moving operations; this exercises
the three call sites that did not: plang_tp_truncate, plang_tp_blockwrite,
and plang_write_binary_turbo (a typed Write). Each of the three cases below
primes the window with an Eof call BEFORE the mutating operation and checks
it again right after -- confirmed against `fpc -Mtp`: all three read TRUE
afterward, not the stale FALSE plang used to report.

All three files are opened read-write (FileMode 2, the r+b-backed shape
that keeps Eof actually consulting the window instead of the
always-write-only-file short circuit) so a real position/window comparison
is exercised, not the trivial output-only case.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:case1 eof-before-truncate=FALSE
CHECK-NEXT:case1 eof-after-truncate=TRUE
CHECK-NEXT:case2 eof-at-5-before-blockwrite=TRUE
CHECK-NEXT:case2 eof-after-blockwrite-to-end=TRUE
CHECK-NEXT:case3 eof-at-5-before-write=TRUE
CHECK-NEXT:case3 eof-after-write-at-end=TRUE
*)

var
  u: file;
  buf: array[0..9] of Byte;
  tf: file of Byte;
  i: Integer;
begin
  { case 1: Truncate at the current position must invalidate the window. }
  assign(u, 'eof-stale-truncate.dat');
  rewrite(u, 1);
  for i := 0 to 9 do buf[i] := i;
  blockwrite(u, buf, 10);
  close(u);
  FileMode := 2;
  reset(u, 1);
  seek(u, 5);
  writeln('case1 eof-before-truncate=', Eof(u));
  truncate(u);
  writeln('case1 eof-after-truncate=', Eof(u));
  close(u);

  { case 2: BlockWrite that lands exactly at the (old) end must invalidate
    the window primed by the Eof call just before it. }
  assign(u, 'eof-stale-blockwrite.dat');
  rewrite(u, 1);
  blockwrite(u, buf, 5);
  close(u);
  FileMode := 2;
  reset(u, 1);
  seek(u, 5);
  writeln('case2 eof-at-5-before-blockwrite=', Eof(u));
  blockwrite(u, buf, 5);
  writeln('case2 eof-after-blockwrite-to-end=', Eof(u));
  close(u);

  { case 3: a typed Write at the end of an r+ typed file must invalidate
    the window the same way. }
  assign(tf, 'eof-stale-typed-write.dat');
  rewrite(tf);
  for i := 0 to 4 do write(tf, Byte(i));
  close(tf);
  FileMode := 2;
  reset(tf);
  seek(tf, 5);
  writeln('case3 eof-at-5-before-write=', Eof(tf));
  write(tf, Byte(99));
  writeln('case3 eof-after-write-at-end=', Eof(tf));
  close(tf);
end.
