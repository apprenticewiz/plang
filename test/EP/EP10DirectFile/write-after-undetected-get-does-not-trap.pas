(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:wrote ok
CHECK-NEXT:x
*)

(* issue #238: get(f) right after rewrite reads from a stream the C level
   opened write-only ("w") -- a direction violation, but plang_get_file
   never checks for one, so it goes undetected here just like the bug
   report describes.  The failed fgetc it makes while trying still sets the
   stream's ferror indicator, and ferror is sticky: it stays set until
   clearerr() runs, not just for the one call that tripped it.  The
   write(f, 'x') that follows genuinely succeeds -- the byte really reaches
   the file, confirmed below by reading it back -- but used to still be
   reported as the same wrong-mode violation get(f) left undetected,
   because trapOnStreamError read that stale flag rather than one reflecting
   its own fputc. *)
program p;
var f: file of char;
    c: char;
begin
  rewrite(f, 'plang_issue238_get_then_write.dat');
  get(f);
  write(f, 'x');
  writeln('wrote ok');
  reset(f, 'plang_issue238_get_then_write.dat');
  read(f, c);
  writeln(c)
end.
