(*
Issue #642: Ord() and an explicit integer typecast on a loose ByteBool/
WordBool/LongBool operand used to ZERO-extend its raw storage bits (via
exprIsSigned, which reads Type::IsSigned == false, deliberately set that
way so a '<'/'>' comparison between two loose Booleans compares their
UNSIGNED magnitude -- see loose-booleans-compare-unsigned.pas). Real
`fpc -Mtp` field practice SIGN-extends instead on the read side: Ord(ByteBool
(200)) is -56 (0xC8 read as a signed i8), not 200, and LongInt(WordBool
(40000)) is -25536 (0x9C40 read as a signed i16), not 40000.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-56
CHECK-NEXT:-25536
CHECK-NEXT:-56
*)

var
  b: ByteBool;
begin
  writeln(Ord(ByteBool(200)));
  writeln(LongInt(WordBool(40000)));
  b := ByteBool(200);
  writeln(Ord(b));
end.
