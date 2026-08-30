(*
SeekEoln(f) skips only blanks and tabs -- unlike SeekEof (see
seekeof-consumes-trailing-whitespace-unlike-a-plain-eof-check.pas, right
next to this test), it does NOT cross a line marker: a line marker is
itself what Eoln tests for, so stopping right before it is the correct
answer, not something to skip past.  Constructed to show this concretely:
after reading "abc" (with nothing but the line marker left on the line),
SeekEoln(f) is TRUE, a following Eoln(f) is STILL true (SeekEoln did not
consume the marker), and the next character actually read is the
space real Turbo Pascal substitutes for a consumed line marker (confirmed
against `fpc -Mtp`: ord of that char is 32), not the first character of the
next line -- proof the line marker itself was not skipped over.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:SeekEoln=TRUE
CHECK-NEXT:eoln after SeekEoln=TRUE
CHECK-NEXT:ord of the next char read=32
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
