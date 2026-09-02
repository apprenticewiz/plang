(*
Issue #593: plang_tp_truncate (runtime/plang_file.cpp) had no direction
check of its own -- it always attempted the raw ftruncate(2) on the
underlying fd, which on Linux fails with EINVAL against a read-only fd,
surfacing as the field-practice-mismatched IOResult 218
(plang_tp_posix_to_run_error's EINVAL mapping).  Real `fpc -Mtp` field
practice reports 103 ("file not open") instead for a Truncate against a
file that was Reset read-only (FileMode 0).  Both correctly refuse to
truncate the file -- only the reported error code used to differ.

Issue #738: the failing Truncate leaves InOutRes pending, which suppresses
every subsequent Turbo I/O call -- including the leading 'truncate io='
string literal of the very writeln that reads IOResult to report it -- so
only the bare '103' reaches stdout, not 'truncate io=103'.  The following
writeln (filesize) runs normally since IOResult's own read already cleared
the pending latch.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:103
CHECK-NEXT:filesize=3
*)

var
  f: file of byte;
begin
  assign(f, 'truncate-on-a-read-only-opened-file.dat');
  rewrite(f);
  write(f, byte(1));
  write(f, byte(2));
  write(f, byte(3));
  close(f);

  FileMode := 0;
  assign(f, 'truncate-on-a-read-only-opened-file.dat');
  reset(f);
  seek(f, 1);
  {$I-}
  truncate(f);
  writeln('truncate io=', IOResult);
  writeln('filesize=', filesize(f));
  close(f);
end.
