(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.out
*)

(*
ERR: write: file is not open in the required mode
*)

(*
OUT-NOT: should not reach here
*)

(* issue #152: the text-file sibling of write-after-reset-traps-on-an-internal-
   file.pas.  An internal (unbound) text file also stays on one tmpfile()
   stream across reset/rewrite, so the same wrong-direction writeln after
   reset must trap, matching a named text file's existing #124 behavior for
   the identical sequence instead of silently succeeding at the C level. *)
program p;
var f: text;
begin
  rewrite(f);
  writeln(f, 'line one');
  reset(f);
  writeln(f, 'should not write this');
  writeln('should not reach here')
end.
