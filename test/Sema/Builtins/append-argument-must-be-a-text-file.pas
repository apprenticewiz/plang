(*
Issue #670: Append(f) used to accept typed and untyped files, opening them
in POSIX append mode -- real `fpc -Mtp` field practice rejects this at
compile time ("Got File, expected Text"), matching every other TP7
line-oriented I/O procedure (Eoln/SeekEoln/SetTextBuf) that requires a Text
file via err_line_proc_not_text.

RUN: %plang -std=turbo -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p;
var
  f: file of byte;
  g: file;
begin
  append(f);
  append(g);
end.

(*
CHECK: 'append' applies to a text file only, not to 'file of Byte'
CHECK: 'append' applies to a text file only, not to 'file'
*)
