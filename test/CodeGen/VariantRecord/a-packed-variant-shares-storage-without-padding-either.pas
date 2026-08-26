(*
The one case in the suite combining packing with a variant part: an
alternative's own fields must round-trip correctly (proving no padding
was inserted between them, and that they share storage with the other
alternative the same way an unpacked variant's do) even when the whole
record is packed.

RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:x 42
CHECK-NEXT:yz
*)

program p(output);
type k = packed record
  a: char;
  case b: boolean of
    true:  (i: integer);
    false: (c: char; d: char)
end;
var v: k;
begin
  v.a := 'x'; v.b := true; v.i := 42; writeln(v.a, ' ', v.i);
  v.b := false; v.c := 'y'; v.d := 'z'; writeln(v.c, v.d)
end.
