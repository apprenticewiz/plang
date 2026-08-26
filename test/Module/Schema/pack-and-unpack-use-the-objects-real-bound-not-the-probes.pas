(*
ISO section 6.7.5.4. The bounds of a schema array are not in its type --
Sema holds the probe's -- so the check on the starting index was made
against "1..-2": one minus the width of z, taken off a probe upper bound
of 1. A bound that describes nothing, refusing a legal program.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[cdef]
CHECK-NEXT:abcdecdefj
*)

program p(output);
type t(n: integer) = record a: array[1..n] of char end;
var q: ^t; z: packed array[1..4] of char; i: integer;
begin new(q, 10);
      for i := 1 to 10 do q^.a[i] := chr(ord('a') + i - 1);
      pack(q^.a, 3, z); writeln('[', z, ']');
      unpack(z, q^.a, 6);
      for i := 1 to 10 do write(q^.a[i]); writeln end.
