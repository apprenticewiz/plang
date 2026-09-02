(*
Issue #662: real Turbo Pascal/`fpc -Mtp` treats a bare CR as a line marker
too, not just LF (Eoln/SeekEoln are TRUE sitting on either byte of a CRLF
pair, or on a lone CR in a bare-CR file; Readln/Read(f,s)/Read(f,c: fixed
array)/Read(f, s: string[N]) all stop there too), AND a character read
positioned on a line marker returns the RAW byte (10 for LF, 13 for CR),
never a substituted space -- unlike ISO/EP's f^, real Turbo Pascal's Text
has no such abstraction. runtime/plang_file.cpp's plang_eoln_file_turbo,
plang_tp_seekeoln, plang_readln_file_turbo, plang_sstr_read_file and
plang_read_file_char_turbo all used to disagree with this (LF-only line
markers, and a space substituted at one), confirmed against the local
`fpc -Mtp` 3.2.2 install before and after the fix.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:c1=65
CHECK-NEXT:c2=66
CHECK-NEXT:eoln-at-CR=TRUE
CHECK-NEXT:c3(at CR)=13
CHECK-NEXT:c4=67
CHECK-NEXT:c5=68
CHECK-NEXT:eoln-at-LF=TRUE
CHECK-NEXT:c6(at LF)=10
CHECK-NEXT:bareCR readln s1=[AB] s2=[CD]
CHECK-NEXT:crlf readln s1=[AB] s2=[CD]
CHECK-NEXT:seekeoln-at-spaces-then-CR=TRUE
*)

var t: text;
    c: char;
    s1, s2: string;
begin
  { CR and LF, individually: eoln true at both, raw bytes at both }
  assign(t, 'text-file-cr-is-a-line-marker-1.dat');
  rewrite(t);
  write(t, 'AB');
  write(t, #13);
  write(t, 'CD');
  write(t, #10);
  write(t, 'EF');
  close(t);

  reset(t);
  read(t, c); writeln('c1=', ord(c));
  read(t, c); writeln('c2=', ord(c));
  if eoln(t) then writeln('eoln-at-CR=TRUE') else writeln('eoln-at-CR=FALSE');
  read(t, c); writeln('c3(at CR)=', ord(c));
  read(t, c); writeln('c4=', ord(c));
  read(t, c); writeln('c5=', ord(c));
  if eoln(t) then writeln('eoln-at-LF=TRUE') else writeln('eoln-at-LF=FALSE');
  read(t, c); writeln('c6(at LF)=', ord(c));
  close(t);

  { a bare CR (no paired LF) is its own, self-contained line marker }
  assign(t, 'text-file-cr-is-a-line-marker-2.dat');
  rewrite(t);
  write(t, 'AB');
  write(t, #13);
  write(t, 'CD');
  close(t);
  reset(t);
  readln(t, s1);
  readln(t, s2);
  writeln('bareCR readln s1=[', s1, '] s2=[', s2, ']');
  close(t);

  { a CRLF pair collapses to ONE line marker, not two }
  assign(t, 'text-file-cr-is-a-line-marker-3.dat');
  rewrite(t);
  write(t, 'AB');
  write(t, #13, #10);
  write(t, 'CD');
  close(t);
  reset(t);
  readln(t, s1);
  readln(t, s2);
  writeln('crlf readln s1=[', s1, '] s2=[', s2, ']');
  close(t);

  { SeekEoln skips leading blanks/tabs and stops at a CR exactly the way it
    already stops at an LF -- it does not cross the marker itself. }
  assign(t, 'text-file-cr-is-a-line-marker-4.dat');
  rewrite(t);
  write(t, '  ');
  write(t, #13);
  write(t, 'CD');
  close(t);
  reset(t);
  if seekeoln(t) then writeln('seekeoln-at-spaces-then-CR=TRUE')
  else writeln('seekeoln-at-spaces-then-CR=FALSE');
  close(t);
end.
