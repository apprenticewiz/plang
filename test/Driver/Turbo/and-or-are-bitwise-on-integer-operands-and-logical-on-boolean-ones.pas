(*
Real Turbo Pascal overloads 'and'/'or': on two Boolean operands they stay
the logical operators ISO/EP already have (and, under Turbo's default
short-circuit switch state (the B directive, off by default), short-
circuit -- see boolean-and/or-under-b-minus...pas); on two
INTEGER operands they are the BITWISE operators instead -- `5 and 3` is 1,
not a type error.  CGBinaryOps::emitBinary's top-of-function block now
peels off the Boolean case (bothBoolean) and falls through everything
else to the generic integer path (the same one Plus/Minus/Times share) for
the bitwise case; Sema::checkBinary's And/Or arm accepts "both Boolean" or,
under -std=turbo only, "both Integer."  Both overloads are exercised here,
not just one, since a change that only got the bitwise half right (or
broke the boolean half) would still pass a bitwise-only test.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
CHECK-NEXT:7
CHECK-NEXT:FALSE
CHECK-NEXT:TRUE
CHECK-NEXT:TRUE
CHECK-NEXT:FALSE
*)

program bitwise_and_or;
begin
  writeln(5 and 3);          { bitwise: 0101 and 0011 = 0001 = 1 }
  writeln(5 or 2);           { bitwise: 0101 or  0010 = 0111 = 7 }
  writeln(true and false);   { logical }
  writeln(true or false);    { logical }
  writeln(true and true);
  writeln(false or false)
end.
