(*
Issue #669: plang_eof_file_turbo (runtime/plang_file.cpp) used to answer
Eof(f) for a TYPED file by peeking one raw BYTE ahead (the same lookahead
window a text file's line-oriented reads use), instead of comparing the
current position against the file's length in units of the file's own
RecSize (SizeOf the element type). A file whose byte length is not an
exact multiple of RecSize -- a trailing partial record -- made this wrong:
the byte peek still saw the stray trailing byte and reported Eof FALSE,
letting one more Read happen and return garbage (io=0) instead of
correctly stopping.

This writes 13 raw bytes (3 whole 4-byte LongInt records plus one trailing
byte) through an UNTYPED file, then re-opens the SAME bytes as `file of
LongInt` and drains it with `while not Eof(f) do Read(f, x)`: real Turbo
Pascal/`fpc -Mtp` (confirmed empirically) stops after exactly 3 records --
floor(12/4) == floor(13/4) == 3 -- not 4.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:total records=3
*)

var
  raw: file;
  b: array[0..12] of Byte;
  f: file of LongInt;
  x: LongInt;
  i, count: Integer;
begin
  assign(raw, 'typed-file-eof-is-record-wise.dat');
  rewrite(raw, 1);
  for i := 0 to 12 do b[i] := i;
  blockwrite(raw, b, 13);
  close(raw);

  assign(f, 'typed-file-eof-is-record-wise.dat');
  reset(f);
  count := 0;
  while not eof(f) do begin
    read(f, x);
    count := count + 1;
  end;
  writeln('total records=', count);
end.
