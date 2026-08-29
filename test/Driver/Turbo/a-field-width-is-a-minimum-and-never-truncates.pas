(*
ISO 7185 §6.9.3.6's TotalWidth truncates a value wider than the field --
`'hello':2` writes just "he" -- but real Turbo Pascal's field widths are
MINIMUMS only: the whole value is always written, and the width just adds
padding when the value is narrower than it.  Confirmed against `fpc -Mtp`:
`'hello':2` writes "hello" in full, and even `'hello':0` -- the one case
ISO/EP write NOTHING for (a zero width still truncates the string-shaped
types down to zero characters there) -- writes "hello" too, since a minimum
of zero is trivially met by any string.  The ISO/EP sibling baseline this
must not have moved is
test/CodeGen/FieldWidth/a-narrow-field-still-truncates-every-type-under-iso7185.pas
and test/EP/FieldWidth/a-narrow-field-still-truncates-every-type-under-extended-pascal.pas.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hello]
CHECK-NEXT:[hello]
CHECK-NEXT:[   hi]
*)

program p;
begin
  write('['); write('hello':2);  writeln(']');
  write('['); write('hello':0);  writeln(']');
  write('['); write('hi':5);     writeln(']')
end.
