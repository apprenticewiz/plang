(*
Turbo string[N] semantics item, concrete work 1: assigning a value longer
than the destination's declared capacity TRUNCATES silently at run time,
the opposite of EP's string(N) -- ISO 10206 6.9.2.2's capacity-error rule
was never Turbo's (see checkStringCapacity's own ShortString early return,
SemaStmt.cpp, and plang_sstr_assign/plang_sstr_from_bytes, runtime/
plang_sstr.cpp).  Covers both a too-long LITERAL (the exact case Sema's
isAssignCompatible rejected outright before this item -- no ShortString
case in its Kind-equality switch) and a too-long ShortString VARIABLE of a
larger declared capacity, so both of emitSstrStore's non-literal and
literal paths (StringCallMarshalling.cpp) are exercised.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var
  s: string[3];
  wide: string[20];
begin
  s := 'hello';
  writeln(s);
  wide := 'a much longer value than three characters';
  s := wide;
  writeln(s);
end.

(*
CHECK:hel
CHECK-NEXT:a m
*)
