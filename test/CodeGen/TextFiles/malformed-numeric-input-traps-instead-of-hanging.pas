(*
RUN: %plang %s -o %t
RUN: not timeout 5 %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.out
*)

(*
ERR: read: input does not start with a valid number
*)

(*
OUT-NOT: should not reach here
*)

(* issue #236: a text file whose next token cannot start an integer used to
   leave read(f, i) consuming nothing and reporting nothing (fscanf's own
   match-failure return was never checked).  Since the file position never
   moved, eof(f) never became true either, so
   `while not eof(f) do read(f, i)` spun forever.  This must now trap
   cleanly and quickly -- well inside the timeout above, which exists only
   as a guard against a regression back to the hang. *)
program p;
var f: text; i: integer;
begin
  i := 0;
  rewrite(f);
  writeln(f, 'xyz');
  reset(f);
  while not eof(f) do read(f, i);
  writeln('should not reach here: ', i)
end.
