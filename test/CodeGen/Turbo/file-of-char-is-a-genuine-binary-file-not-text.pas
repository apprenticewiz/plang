(*
Tier 3 Cluster C item 5: real Turbo Pascal gives `text` its own distinct
predefined type (Borland's manual: "the standard type Text ... is not the
same as File Of Char") and treats `file of char` as an ordinary typed BINARY
file -- each Char component is one raw byte, no line-ending/formatting
convention applies.  This is DIFFERENT from ISO 7185/Extended Pascal, where
§6.4.3.5 gives `file of char` no separate identity from `text` at all ("a
file of the type char is termed a textfile") -- see the sibling test just
below (a-file-of-char-is-still-text-under-iso-and-ep.pas) for that pinned,
UNCHANGED ISO/EP behavior.

Two chars are DELIBERATELY not enough to distinguish "2 raw bytes" from "2
characters written through the text path" -- for two plain chars the two
representations happen to look identical.  What actually distinguishes them
is what happens on close(): the text path (plang_close's closeFinalLine, via
plang_write_binary's ISO/EP twin never touched here) appends a trailing
newline to finish an unterminated line; the raw binary path
(plang_write_binary_turbo) does not.  So this checks the file is EXACTLY 2
bytes, 0x41 0x42 -- if the text path were used by mistake, a third byte
(0x0a) would show up.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t
RUN: wc -c < file-of-char-is-a-genuine-binary-file-not-text.dat | tr -d ' ' | FileCheck --check-prefix=SIZE %s
RUN: od -An -tx1 file-of-char-is-a-genuine-binary-file-not-text.dat | FileCheck %s
*)

(* GNU od (Linux) and BSD od (macOS) pad columns differently, so this checks
   content loosely (default FileCheck whitespace handling, no --match-full-
   lines) and instead pins the byte COUNT exactly via `wc -c`, which is
   portable and is what actually distinguishes "2 raw bytes" from "2 bytes
   plus a text-mode trailing newline" -- the thing this test exists to catch. *)
(*
SIZE: 2
CHECK: 41 42
*)

var f: file of char;
begin
  assign(f, 'file-of-char-is-a-genuine-binary-file-not-text.dat');
  rewrite(f);
  write(f, 'A');
  write(f, 'B');
  close(f);
end.
