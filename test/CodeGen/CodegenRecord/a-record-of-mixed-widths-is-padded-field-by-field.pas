(*
Six fields, six different widths, one of them an embedded packed
sub-array -- each boundary needs its own pad computation, and a wrong one
anywhere either fails Sema's own agreement check against the layout
(an internal error) or puts a field at the wrong offset. Every field is
round-tripped, not just compiled, so a wrong offset shows up as a wrong
value rather than merely as a successful build.

RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:x 3 true 1.5 yz 9
*)

program p(output);
type r = record a: char; b: integer; c: boolean; d: real;
                e: packed array[1..3] of char; f: integer end;
var v: r;
begin
  v.a := 'x'; v.b := 3; v.c := true; v.d := 1.5; v.e[1] := 'y'; v.e[3] := 'z'; v.f := 9;
  writeln(v.a, ' ', v.b, ' ', v.c, ' ', v.d:0:1, ' ', v.e[1], v.e[3], ' ', v.f)
end.
