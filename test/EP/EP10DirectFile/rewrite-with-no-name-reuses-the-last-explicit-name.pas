(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:second
*)

(* issue #239: rewrite(f) with no name, after an earlier rewrite(f, 'name'),
   must reopen -- and, per ordinary rewrite semantics, truncate -- that same
   external file rather than silently diverting to fresh, unnamed internal
   storage. Before the fix, the second rewrite closed 'named2.txt' (leaving
   'first' sitting on disk, untouched) and sent 'second' into an anonymous
   tmpfile() that vanished the moment it was closed; reopening the file by
   its real name afterward read back 'first', not 'second'. *)

program p;
var f: text; s: string(20);
begin
  rewrite(f, 'plang_issue239_named2.txt'); writeln(f, 'first');
  rewrite(f); writeln(f, 'second');
  close(f);
  reset(f, 'plang_issue239_named2.txt'); readln(f, s); close(f);
  writeln(s)
end.
