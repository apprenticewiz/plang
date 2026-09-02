(*
Issue #680: a char-by-char Read(f, ch) past the end of a Turbo text file used
to answer chr(0) once F->Buf hit EOF (runtime/plang_file.cpp's
plang_read_file_char_turbo).  `fpc -Mtp` 3.2.2's text driver answers #26
(Ctrl-Z / SUB, the classic DOS end-of-file marker) instead, and keeps
answering it for every further read past the end -- confirmed empirically by
reading two bytes past the end of a two-byte file.  ISO/EP's own char-read
path (plang_read_file_char, no `_turbo` suffix) is a separate, dialect-
agnostic ISO 7185 choice this item does not touch.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:65
CHECK-NEXT:66
CHECK-NEXT:26
CHECK-NEXT:26
*)

var
  f: text;
  c: char;
  i: integer;
begin
  assign(f, 'char-read-at-eof-returns-ctrl-z-not-nul-issue-680.txt');
  rewrite(f);
  write(f, 'AB');
  close(f);

  reset(f);
  for i := 1 to 4 do begin
    read(f, c);
    writeln(ord(c));
  end;
  close(f);
end.
