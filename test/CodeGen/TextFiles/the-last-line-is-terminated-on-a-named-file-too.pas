(*
RUN: %plang %s -o %t
RUN: %run %t
RUN: echo -n MARK >> issue234-named.txt
RUN: cat issue234-named.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(* issue #234: closeFinalLine (see the-last-line-is-terminated-even-without-
   a-writeln.pas, right next to this test) finished the last line of the one
   kind of file rewrite(f) with no name opens -- an internal, tmpfile()-
   backed file, always "w+b" -- but was dead code for a NAMED file: opened
   "w" (write-only, so a stray Pascal-level read against it traps at the C
   level instead of silently succeeding -- issue #124), fgetc against that
   stream always failed and read back exactly like "the file already ends in
   a marker", so nothing was ever appended, and this was also the only
   caller closeFinalLine had at all -- close(f) below did not reach it
   either. Appending MARK straight onto the raw file after the program exits
   shows the difference directly: fixed, it lands on a line of its own;
   broken, it glues onto "abc" with no marker between them. *)

(*
CHECK:abc
CHECK-NEXT:MARK
*)

program p;
var f: text;
begin
  rewrite(f, 'issue234-named.txt');
  write(f, 'abc');
  close(f)
end.
