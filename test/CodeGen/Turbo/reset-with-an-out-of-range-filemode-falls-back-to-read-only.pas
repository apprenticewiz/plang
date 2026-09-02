(*
Issue #589: plang_tp_reset (runtime/plang_file.cpp) used to treat any
FileMode other than exactly 0 as read-write, including out-of-range values
(anything other than 0, 1, or 2) -- so FileMode 99 opened a file read-write,
letting a following Write silently succeed.  Real `fpc -Mtp` field practice
instead falls back to read-only for any out-of-range FileMode, matching
FileMode 0's own behavior: confirmed the Reset itself still succeeds, but a
following Write fails with IOResult 105 ("file not open for output").

Issue #738: the failing Write leaves InOutRes pending, which suppresses
every subsequent Turbo I/O call -- including the leading 'write io=' string
literal of the very writeln that reads IOResult to report it (the same
"first write attempt in the statement to actually clear InOutRes" effect
documented on text-file-reset-ignores-filemode-typed-file-reset-still-
honors-it.pas) -- so only the bare '105' reaches stdout, not 'write io=105'.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:reset io=0
CHECK-NEXT:105
*)

var
  f: text;
begin
  assign(f, 'reset-with-an-out-of-range-filemode.dat');
  rewrite(f);
  writeln(f, 'hello');
  close(f);

  FileMode := 99;
  assign(f, 'reset-with-an-out-of-range-filemode.dat');
  {$I-}
  reset(f);
  writeln('reset io=', IOResult);
  {$I-}
  writeln(f, 'world');
  writeln('write io=', IOResult);
  close(f);
end.
