(*
Issue #668: plang_tp_rewrite (runtime/plang_file.cpp) used to open a typed
or untyped file's Rewrite write-only ("w", Readable=0) -- so a following
Seek(f,0); Read(f, x) failed with IOResult 104 ("file not open for
input"), and Eof(f) stayed unconditionally TRUE (plang_eof_file_turbo's own
`!F->Readable -> return 1` short circuit) even after a Write and a Seek
back to a position that genuinely holds a record. Real Turbo Pascal/
`fpc -Mtp` opens Rewrite read-write UNCONDITIONALLY (confirmed empirically:
this holds for FileMode 0/1/2 alike -- Rewrite, unlike Reset, does not
condition its open mode on FileMode at all), so Write/Seek(0)/Read against
a freshly Rewrite-opened typed file succeeds, and Eof correctly tracks the
file's actual record position once there is one to track.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:eof-after-rewrite=TRUE
CHECK-NEXT:eof-after-write-seek0=FALSE
CHECK-NEXT:read io=0 i=42
*)

var f: file of longint;
    i, j: longint;
begin
  assign(f, 'typed-file-rewrite-opens-read-write.dat');
  rewrite(f);
  if eof(f) then writeln('eof-after-rewrite=TRUE')
  else writeln('eof-after-rewrite=FALSE');

  j := 42;
  write(f, j);
  seek(f, 0);
  if eof(f) then writeln('eof-after-write-seek0=TRUE')
  else writeln('eof-after-write-seek0=FALSE');

  {$I-}
  read(f, i);
  writeln('read io=', IOResult, ' i=', i);
end.
