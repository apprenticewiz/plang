(*
Tier 3 Cluster C item 6, Group B/A together: Seek(f, n) positions f at
record n (0-relative, in units of f's own RecSize -- SizeOf(Byte) = 1
here), and a following FilePos(f) reads back exactly n; FileSize(f)
independently reports the file's own total record count.  Confirmed
against `fpc -Mtp` before this test was written.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:FileSize=5
CHECK-NEXT:FilePos after seek(2)=2
CHECK-NEXT:value at record 2=3
CHECK-NEXT:FilePos after read=3
*)

var
  f: file of Byte;
  v: Byte;
  i: Byte;
begin
  assign(f, 'seek-filepos-and-filesize-agree-on-a-real-file.bin');
  rewrite(f);
  for i := 1 to 5 do write(f, i);
  close(f);

  reset(f);
  writeln('FileSize=', filesize(f));
  seek(f, 2);
  writeln('FilePos after seek(2)=', filepos(f));
  read(f, v);
  writeln('value at record 2=', v);
  writeln('FilePos after read=', filepos(f));
  close(f);
end.
