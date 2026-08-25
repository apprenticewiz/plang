(*
ISO section 6.7.5.4. The bounds of a schema array are not in its type --
Sema holds the probe's -- so the check on the starting index was made
against "1..-2": one minus the width of z, taken off a probe upper bound
of 1. A bound that describes nothing, refusing a legal program.
*)

(* And the check still refuses an index that really is out of range, now
   naming the bound the object actually has rather than the probe's. *)

(*
RUN: %plang -std=iso10206 -frange-checks %s -o %t
RUN: not %run %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 1..7
*)

program p(output);
type t(n: integer) = record a: array[1..n] of char end;
var q: ^t; z: packed array[1..4] of char;
begin new(q, 10); pack(q^.a, 9, z) end.
