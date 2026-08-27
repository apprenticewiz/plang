(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:alpha
CHECK-NEXT:alpha
*)

(* issue #239: the same retained-name gap plang_rewrite had also applied to
   plang_reset -- a name given directly to reset(f, 'name') was never kept
   anywhere a later name-less reset(f) could find, so (once the file had
   been closed, leaving nothing open to just rewind in place) that second
   reset silently fell back to fresh, empty internal storage instead of
   reopening the same external file. *)

program p;
var f: text; s1, s2: string(20);
begin
  rewrite(f, 'plang_issue239_reset.txt'); writeln(f, 'alpha'); close(f);
  reset(f, 'plang_issue239_reset.txt'); readln(f, s1); close(f);
  reset(f); readln(f, s2); close(f);
  writeln(s1);
  writeln(s2)
end.
