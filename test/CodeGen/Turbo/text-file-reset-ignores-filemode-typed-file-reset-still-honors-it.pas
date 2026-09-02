(*
Issue #667: plang_tp_reset (runtime/plang_file.cpp) used to honor the
CURRENT value of FileMode for EVERY Reset, text files included -- but real
Turbo Pascal/`fpc -Mtp` ignores FileMode entirely for a `text` file, always
opening it read-only (a following Write sets IOResult 105, "file not open
for output", no matter what FileMode currently holds); FileMode only ever
governs Reset's open mode for a typed or untyped file. Confirmed against
the local `fpc -Mtp` 3.2.2 install both ways: this checks FileMode 1
(write-only) against a text file (must still read fine, and a write must
still fail 105 -- the opposite of what FileMode 1 would mean for a typed
file, exercised right after for contrast) and FileMode 2 (read-write, the
documented default) against the SAME text file (still read-only: writing
must still fail 105).

The fix's own fallout (see that function's own comment): plang_writeln_file_
turbo used to call the ISO/EP-only, unconditionally-aborting trapOnWrongDirection/
trapOnStreamError instead of this file's tpTrapOnWrongDirection/
tpTrapOnStreamError -- latent until this fix (a text Reset under FileMode 1
used to open write-only, so a following Writeln never hit the
wrong-direction case), it would have made the FileMode-1 write attempt
below abort the whole process instead of setting IOResult 105 under
`{$I-}`. Fixed alongside #667 itself; this test's own `write io=105` lines
are what would have aborted instead had that fallout not been fixed too.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:fm1: reset io=0
CHECK-NEXT:fm1: read io=0 s=hello
CHECK-NEXT:fm1: write io=105
CHECK-NEXT:fm2: reset io=0
CHECK-NEXT:fm2: read io=0 s=hello
CHECK-NEXT:fm2: write io=105
CHECK-NEXT:typed fm1: read io=104 i=0
*)

var t: text;
    s: string;
    f: file of longint;
    i, j: longint;
begin
  assign(t, 'text-file-reset-ignores-filemode.dat');
  rewrite(t);
  writeln(t, 'hello');
  close(t);

  FileMode := 1;
  assign(t, 'text-file-reset-ignores-filemode.dat');
  {$I-}
  reset(t);
  writeln('fm1: reset io=', IOResult);
  {$I-}
  readln(t, s);
  writeln('fm1: read io=', IOResult, ' s=', s);
  {$I-}
  writeln(t, 'world');
  writeln('fm1: write io=', IOResult);

  FileMode := 2;
  assign(t, 'text-file-reset-ignores-filemode.dat');
  {$I-}
  reset(t);
  writeln('fm2: reset io=', IOResult);
  {$I-}
  readln(t, s);
  writeln('fm2: read io=', IOResult, ' s=', s);
  {$I-}
  writeln(t, 'world');
  writeln('fm2: write io=', IOResult);

  { Contrast: FileMode 1 DOES still govern a typed file's Reset -- opens
    write-only, so a following Read fails 104 -- proving this fix scoped
    the change to text files only, not a blanket "ignore FileMode" change. }
  FileMode := 1;
  assign(f, 'text-file-reset-ignores-filemode-typed.dat');
  rewrite(f);
  j := 99;
  write(f, j);
  close(f);
  reset(f);
  {$I-}
  read(f, i);
  writeln('typed fm1: read io=', IOResult, ' i=', i);
end.
