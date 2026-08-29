(*
Seek(f, n) past the file's current end is LEGAL in real Turbo Pascal --
confirmed against `fpc -Mtp`: FilePos reads back n with IOResult 0, no
error of any kind, and this is the documented idiom for EXTENDING a file
(seek past the end, then write -- the following write fills the gap with
zero bytes exactly the way an ordinary POSIX sparse-seek-then-write does).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:FilePos=10 IOResult=0
CHECK-NEXT:FileSize after extending write=11
*)

var
  f: file of Byte;
begin
  assign(f, 'seek-past-the-current-end-of-file-is-not-an-error.bin');
  rewrite(f);
  write(f, Byte(1));
  write(f, Byte(2));
  seek(f, 10);
  writeln('FilePos=', filepos(f), ' IOResult=', IOResult);
  write(f, Byte(99));
  close(f);

  reset(f);
  writeln('FileSize after extending write=', filesize(f));
  close(f);
end.
