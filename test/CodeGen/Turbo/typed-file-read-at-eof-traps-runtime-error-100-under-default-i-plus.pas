(*
Issue #661: a typed-file Read at (or past) end-of-file used to record NO
error at all -- runtime/plang_file.cpp's plang_read_binary_turbo called
fread and then only tpTrapOnStreamError (ferror()-based, correct for its
OWN job of catching a wrong-direction stream), with nothing checking
fread's own short-read return. A short/at-EOF fread sets the C stream's
feof indicator, not ferror -- so InOutRes stayed 0 and the destination
(pre-zeroed at function entry) silently read back as 0, even under
Turbo's default `{$I+}`. Real Turbo Pascal/`fpc -Mtp` traps this with
Runtime error 100 ("disk read error") -- confirmed against the local
`fpc -Mtp` 3.2.2 install -- exercised here with no `{$I}` directive at
all, so the second Read (one past the single record this file holds) must
abort the process right there, through the same plang_tp_runerror(Code)
reporter every other Turbo runtime check uses, exit status 100 (InOutRes's
own code for this condition).

RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 100 %run %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 100 at $
*)

var f: file of integer; x, y: integer;
begin
  y := 42;
  assign(f, 'typed-file-read-at-eof-traps-runtime-error-100.dat');
  rewrite(f);
  write(f, y);
  reset(f);
  read(f, x);           { the file's one and only record: succeeds }
  read(f, x);           { past EOF: must trap Runtime error 100 here }
  writeln('unreachable: default {$I+} should have aborted above');
end.
