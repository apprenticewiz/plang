(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: outside a string of length 5
*)

(* Regression test for issue #220: plang_str_substr_assign's past-the-end
   check reads `i+n-1>ld`, computed directly -- for a huge, independently-
   reachable i and n (i is whatever the low index-expression evaluates to;
   n arrives as high-low+1, computed by CGAssign's plain wrapping add/sub,
   so it can come out small even when low and high are themselves both
   ordinary in-range Integer values) the addition signed-overflows and
   wraps around to a value that passes the check. Worse than the plain UB
   report this can produce: for i = maxint and n = 5 specifically, the
   wraparound also happens to satisfy the *next* guard (the assigned
   value's length must equal n), so nothing stops the fall-through to
   memcpy with i-1 as a many-exabyte offset -- a real out-of-bounds write,
   not merely an abstract signed-overflow report. Before the fix this
   segfaults (i and n are read into variables, not passed as literals, so
   the wrapping subtraction that derives n is a genuine runtime
   computation, not something a constant-folding pass could shortcut). *)
program p;
var t: string(20); w: string(5); i, j: integer;
begin
  t := 'hello';
  w := 'WORLD';
  i := 9223372036854775807;
  j := -9223372036854775805;
  t[i..j] := w;
  writeln(t)
end.
