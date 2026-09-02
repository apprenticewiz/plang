(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
*)

(* Same root cause as issue #774 (registerEnumValues not recursing into
   every type-denoter shape that can carry an anonymous inline enum), but
   for a file's element type rather than a set's base type. *)
program p(output);
var f: file of (lo, mid, hi);
begin
  rewrite(f);
  f^ := mid;
  put(f);
  reset(f);
  writeln(ord(f^))
end.
