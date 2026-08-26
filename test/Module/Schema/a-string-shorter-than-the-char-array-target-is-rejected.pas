(*
EP section 6.4.3.2 wants the lengths equal. Sema settles that when it knows
the capacity and cannot when a discriminant fixes one, so it lets the
assignment through -- and copying the array's length out of a shorter
string read past the end of the allocation and dropped heap bytes into
the array. A read overrun, introduced by the compatibility rule that
made this assignment legal in the first place.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: cannot fill
*)

program p(output);
type ps = ^string;
var q: ps; a: packed array[1..40] of char;
begin new(q, 4); q^ := 'ab'; a := q^ end.
