(*
Tier 3 Cluster C item 5's own contrast case, ISO side.  ISO 7185 §6.4.3.5
gives `file of char` no separate identity from `text` at all ("a file of the
type char is termed a textfile"), and this project's own existing pinned
test (a-file-of-char-is-a-text.pas, same directory) already covers that. This
test pins the CONCRETE, empirically-confirmed consequence: writing two chars
to a `file of char` and closing it produces 3 bytes, "AB\n" -- the trailing
newline plang_close's closeFinalLine appends to finish an unterminated TEXT
line, per the same text-file convention `write(output, 'x')` follows.

-std=turbo gives `file of char` a genuinely separate identity from `text`
(Borland's manual: "the standard type Text ... is not the same as File Of
Char") and treats it as an ordinary typed BINARY file instead -- see the
sibling test in test/CodeGen/Turbo/
(file-of-char-is-a-genuine-binary-file-not-text.pas), which pins the SAME
two-char write producing exactly 2 raw bytes, 0x41 0x42, no newline, under
-std=turbo.  Two chars alone would look identical between "raw bytes" and
"formatted text" -- what actually tells the two apart is this trailing
newline, present here and absent there.

RUN: %plang %s -o %t
RUN: %run %t
RUN: od -An -tx1 a-file-of-char-is-still-text-under-iso-and-ep-not-turbo.dat | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 41 42 0a
*)

program p(output);
var f: file of char;
begin
  rewrite(f, 'a-file-of-char-is-still-text-under-iso-and-ep-not-turbo.dat');
  write(f, 'A');
  write(f, 'B');
  close(f);
end.
