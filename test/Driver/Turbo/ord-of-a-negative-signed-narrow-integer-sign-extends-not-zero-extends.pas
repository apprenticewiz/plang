(*
Issue #632: Ord(x)'s codegen unconditionally CreateZExt'd its argument to
i64 -- correct for Char/Boolean/Enum and Turbo's unsigned sized-integer
rungs (Byte/Word/Cardinal/LongWord/QWord), but wrong for a SIGNED narrow
argument: ISO §6.6.6.2 makes Ord(x) x's own ordinal number, i.e. x's own
value read as an integer, so Ord of a negative ShortInt/Integer/LongInt
must stay negative, not turn into a large positive number by reading its
sign bit as a magnitude bit instead.  (ISO 7185/Extended Pascal's own
single Integer type is unaffected: it is always Width 64 and never reaches
this codepath's ZExt at all, always taking ToI64's own no-op fast path.)

Cross-checked against `fpc -Mtp` (3.2.2): with i: Integer := -1 and
s: ShortInt := -1, Ord(i) and Ord(s) are both -1 -- plang used to print
65535 and 255 instead (i.e. -1's own 16-bit and 8-bit two's-complement bit
patterns, read back as unsigned).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-1 -1
*)

program ord_signed_narrow;
var
  i: Integer;
  s: ShortInt;
begin
  i := -1;
  s := -1;
  writeln(Ord(i), ' ', Ord(s));
end.
