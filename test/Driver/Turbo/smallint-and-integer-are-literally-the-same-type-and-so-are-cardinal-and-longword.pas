(*
TypeContext::getInt interns by width and signedness alone (see its own
comment), so `SmallInt` (16, signed) is not merely compatible with `Integer` -- it is
the SAME Type object, and likewise `LongWord` (32, unsigned) and `Cardinal`.
This is the deliberate design this Tier 2 feature ships with, not a gap:
assigning a variable of one spelling directly to a variable of the other
needs no cast and raises no diagnostic, exactly as assigning a variable to
another of its own declared type would not.  See
assigning-an-incompatible-value-to-a-sized-integer-type-names-the-declared-type-not-integer.pas
for how a real type MISMATCH still gets a sensible name out of one shared
Type object.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1234
CHECK-NEXT:-4321
CHECK-NEXT:987654321
CHECK-NEXT:123456789
*)

program p;
var
  i:  Integer;
  s:  SmallInt;
  c:  Cardinal;
  lw: LongWord;
begin
  s := 1234;
  i := s;
  writeln(i);
  i := -4321;
  s := i;
  writeln(s);

  lw := 987654321;
  c := lw;
  writeln(c);
  c := 123456789;
  lw := c;
  writeln(lw)
end.
