(*
Real Turbo Pascal writes a Boolean's textual form in UPPERCASE (TRUE/FALSE),
the reverse of ISO 7185/EP's lowercase (true/false) -- confirmed against
`fpc -Mtp`.  Exercises every write shape the spelling has to reach: the
default (unwidthed) writer, an explicit width narrower than the spelling
(paired with the non-truncating-width reversal: TRUE/FALSE are written in
full even so -- see a-field-width-is-a-minimum-and-never-truncates.pas for
that reversal on its own), and a width wider than the spelling.  The ISO/EP
sibling baseline this must not have moved is
test/CodeGen/FieldWidth/a-boolean-still-writes-lowercase-under-iso7185.pas
and test/EP/FieldWidth/a-boolean-still-writes-lowercase-under-extended-pascal.pas.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:TRUE
CHECK-NEXT:FALSE
CHECK-NEXT:[TRUE]
CHECK-NEXT:[FALSE]
CHECK-NEXT:[      TRUE]
*)

program p;
begin
  writeln(true);
  writeln(false);
  write('['); write(true:2);   writeln(']');
  write('['); write(false:3);  writeln(']');
  write('['); write(true:10);  writeln(']')
end.
