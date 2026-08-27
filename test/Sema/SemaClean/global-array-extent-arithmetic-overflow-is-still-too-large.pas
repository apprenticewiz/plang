(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(* issue #214: byteSizeOf used to compute an array's element count as
   "SubHi - SubLo + 1" in plain int64_t, which is signed-overflow UB once the
   bounds are far enough apart -- array[0..maxint] included, since maxint IS
   int64_t's own upper bound.  The wrapped result let the array slip past
   the "count <= 0" rejection and past the 1 GiB global-variable gate this
   function feeds, so neither declaration below used to be diagnosed at all. *)

program p;
var
  a : array [0..maxint] of integer;
  b : array [-maxint..maxint] of char;
begin
  a[1] := 0;
  b[1] := 'x'
end.

(*
CHECK: 'a' is too large to be a global variable
CHECK: 'b' is too large to be a global variable
*)
