(*
Tier 3 gap fix sibling: seek-filepos-filesize-truncate-combined-scenario.pas
(test/Turbo/) could only exercise Seek+Truncate inside the ORIGINAL
Rewrite-opened session, because plang_tp_reset used to always open "r"
(read-only) regardless of FileMode -- Seek+Truncate against a
Reset-reopened file genuinely EINVALed at the OS level. Now that
plang_tp_reset honors FileMode's own read-write default (2) -- see
reset-opens-read-write.pas for that fix on its own -- the exact same
Seek+Truncate idiom works against a file the program only ever CLOSED and
RESET, never kept open from Rewrite, which is the more common real-world
shape ("open an existing file, patch a record in place, drop the tail").

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
RUN: wc -c < seek-truncate-after-fresh-reset.bin | tr -d ' ' | FileCheck --check-prefix=SIZE %s
*)

(*
CHECK:FileSize after rewrite=10
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
  assign(f, 'seek-truncate-after-fresh-reset.bin');
  rewrite(f);
  for i := 1 to 10 do write(f, Byte(i * 10));
  close(f);

  { Everything below happens in a FRESH Reset session -- never the
    Rewrite session that created the file. }
  reset(f);
  writeln('FileSize after rewrite=', filesize(f));

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
