(*
ISO 7185 §6.10.3.1(u): a zero TotalWidth for a char write means the field
holds exactly zero characters, so `c:0` writes nothing.  Real Turbo Pascal
writes the character regardless of the requested width -- confirmed against
`fpc -Mtp`: `write('x':0)` writes "x".  The sibling ISO/EP baselines this
must not have moved are
test/CodeGen/FieldWidth/a-zero-width-char-still-writes-nothing-under-iso7185.pas
and test/EP/FieldWidth/an-explicit-zero-width-is-still-unaffected.pas.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[x]
*)

program p;
begin write('['); write('x':0); writeln(']') end.
