(*
SeekEof(f) is a CONSUMING operation, unlike the plain Eof(f) it is built on
top of -- it skips (actually reads past, not merely peeks through) any
blanks, tabs, carriage returns and line markers ahead of the current
position before testing eof.  Constructed to show the difference concretely:
after reading "abc", plain Eof(f) is still FALSE (a blank line -- three
spaces and a line marker -- remains unread), but SeekEof(f) is TRUE (having
consumed that trailing whitespace and line marker to reach the real end of
file), and a following Eof(f) is now ALSO true -- proving SeekEof genuinely
moved the file's position rather than merely computing an answer.  Confirmed
against `fpc -Mtp` before this test was written.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:after reading abc: eof=FALSE
CHECK-NEXT:SeekEof=TRUE
CHECK-NEXT:eof after SeekEof=TRUE
*)

var
  f: text;
  c: Char;
begin
  assign(f, 'seekeof-consumes-trailing-whitespace-unlike-a-plain-eof-check.txt');
  rewrite(f);
  write(f, 'abc');
  writeln(f);
  write(f, '   ');
  writeln(f);
  close(f);

  reset(f);
  read(f, c); read(f, c); read(f, c);
  if eof(f) then writeln('after reading abc: eof=TRUE')
  else writeln('after reading abc: eof=FALSE');
  if seekeof(f) then writeln('SeekEof=TRUE') else writeln('SeekEof=FALSE');
  if eof(f) then writeln('eof after SeekEof=TRUE')
  else writeln('eof after SeekEof=FALSE');
  close(f);
end.
