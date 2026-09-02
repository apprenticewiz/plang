(*
SeekEoln(f) skips only blanks and tabs -- unlike SeekEof (see
seekeof-consumes-trailing-whitespace-unlike-a-plain-eof-check.pas, right
next to this test), it does NOT cross a line marker: a line marker is
itself what Eoln tests for, so stopping right before it is the correct
answer, not something to skip past.  Constructed to show this concretely:
after reading "abc" (with nothing but the line marker left on the line),
SeekEoln(f) is TRUE, a following Eoln(f) is STILL true (SeekEoln did not
consume the marker), and the next character actually read is the LINE
MARKER ITSELF, RAW (issue #662: confirmed against `fpc -Mtp`, ord of that
char is 10 -- real Turbo Pascal's Text has no ISO-style "line marker reads
as a space" substitution at all, every byte on disk including the '\n' is a
character Read(f, c) can return), not the first character of the next line
-- proof the line marker itself was not skipped over.

Issue #662 note: this test's own original expectation (ord 32, a space) was
itself wrong -- re-confirmed against a fresh local `fpc -Mtp` 3.2.2 run
while fixing that issue, not merely asserted -- runtime/plang_file.cpp's
plang_read_file_char_turbo used to apply ISO §6.4.3.5's line-marker-as-space
substitution, which is an ISO/EP rule this dialect never actually has.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:SeekEoln=TRUE
CHECK-NEXT:eoln after SeekEoln=TRUE
CHECK-NEXT:ord of the next char read=10
*)

var
  f: text;
  c: Char;
begin
  assign(f, 'seekeoln-skips-blanks-and-tabs-but-does-not-cross-the-line-marker.txt');
  rewrite(f);
  write(f, 'abc');
  writeln(f);
  write(f, 'def');
  close(f);

  reset(f);
  read(f, c); read(f, c); read(f, c);
  if seekeoln(f) then writeln('SeekEoln=TRUE') else writeln('SeekEoln=FALSE');
  if eoln(f) then writeln('eoln after SeekEoln=TRUE')
  else writeln('eoln after SeekEoln=FALSE');
  read(f, c);
  writeln('ord of the next char read=', ord(c));
  close(f);
end.
