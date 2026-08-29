(*
Erase(f) deletes the file f is bound to (by Assign) -- checked two ways:
the shell-level `test -f` right after the program exits (the file is
genuinely gone from disk, not merely closed or truncated to empty), and,
from inside the SAME program, a following Reset(f) reporting InOutRes 2
("file not found") -- proof the deletion is visible immediately, not just
after the process exits.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
RUN: not test -f erase-deletes-the-bound-file-from-disk.dat
*)

(*
CHECK:erase IOResult=0
CHECK-NEXT:reset after erase IOResult=2
*)

var
  f: file of Byte;
begin
  assign(f, 'erase-deletes-the-bound-file-from-disk.dat');
  rewrite(f);
  write(f, Byte(42));
  close(f);

  erase(f);
  writeln('erase IOResult=', IOResult);

  {$I-}
  reset(f);
  writeln('reset after erase IOResult=', IOResult);
  {$I+}
end.
