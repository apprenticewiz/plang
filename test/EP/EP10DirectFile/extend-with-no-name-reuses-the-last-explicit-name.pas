(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:first
CHECK-NEXT:second
*)

(* issue #411: unlike plang_reset/plang_rewrite (fixed by issue #239 to
   retain and reuse a prior explicit name via setBinding()/findBinding()),
   plang_extend and plang_update never called findBinding() at all -- a
   name-less extend/update branched straight to an internal-tmpfile path
   regardless of any name the file had previously been opened with, so data
   written through it silently vanished (the tmpfile is discarded on close)
   instead of appending to the real, on-disk file.
   Concretely: rewrite(f, name) writes 'first' and closes; extend(f) with no
   name must reopen and append to that same named file, not divert to a
   fresh anonymous tmpfile -- so a subsequent reset(f, name) has to read
   back both 'first' and 'second'. *)
program p;
var f: text; s1, s2: string(40);
begin
  rewrite(f, 'plang_issue411_extend.txt'); writeln(f, 'first'); close(f);
  extend(f); writeln(f, 'second'); close(f);
  reset(f, 'plang_issue411_extend.txt');
  readln(f, s1);
  readln(f, s2);
  close(f);
  writeln(s1);
  writeln(s2)
end.
