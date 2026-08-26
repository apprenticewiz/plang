(*
Found in the acceptance test: eight bytes short, all of it in the tail
padding, where no field would ever have shown it. The record's overall
size has to round up to the alignment of its widest member (16, from the
embedded set of char) even though the LAST field only needs 8-byte
alignment on its own -- codegen and Sema's own byteSizeOf disagreeing
about that tail padding is an internal error, so simply compiling this
shape is the whole test.

RUN: %plang %s -o %t
RUN: %run %t
*)

program p(output);
type r = record i: integer; s: set of char; p: ^integer end;
var v: r;
begin v.i := 1; writeln(v.i) end.
