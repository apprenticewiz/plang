(*
Issue #795: an integer literal past Int64::max (9223372036854775807) but
within UInt64's own range (up to 18446744073709551615) used to be rejected
outright by the parser -- Parser::parseFactor's std::from_chars into
IntLitExpr::Value (int64_t) overflowed and the literal was refused before
Sema ever saw which type it was headed for, so a QWord value in the upper
half of its range could not be written as a literal at all, only built via a
typecast (QWord(-1), QWord(9223372036854775807) + 1 -- see this directory's
own qword-write-and-read-round-trip-the-full-unsigned-range.pas, whose
introductory comment documented that exact gap). `fpc -Mtp` 3.2.2 accepts
such a literal directly, no typecast needed, whenever the destination can
itself hold it: bare in a writeln argument (formatted unsigned, matching the
value the text denotes) and assigned straight to a QWord variable.  Both
forms are exercised here, at both the exact UInt64::max boundary and the
exact Int64::max+1 boundary (2^63, the smallest value that no longer fits a
signed 64-bit destination).

Also covers passing such a literal bare as a value-parameter actual to a
QWord-typed formal (Sema::checkCallArgs' value-parameter arm, the same kind
of destination-aware call site as the assignment case, and the one place
this fix's own regression sweep found not wired up on the first pass: a
value-parameter formal is judged through isAssignCompatible, which only
compares TypeKind::Integer by kind and not by width/signedness, so without
this the literal's two's-complement bit pattern would have passed silently
through to ANY Integer-kind formal, not only a QWord one).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:18446744073709551615
CHECK-NEXT:9223372036854775808
CHECK-NEXT:18446744073709551615
CHECK-NEXT:9223372036854775808
CHECK-NEXT:18446744073709551615
*)

program qwordLiteralPastInt64Max;
var
  q: QWord;

procedure PrintQWord(v: QWord);
begin
  writeln(v);
end;

begin
  writeln(18446744073709551615);
  writeln(9223372036854775808);
  q := 18446744073709551615;
  writeln(q);
  q := 9223372036854775808;
  writeln(q);
  PrintQWord(18446744073709551615);
end.
