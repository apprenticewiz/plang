(*
Turbo's Integer is 16-bit (LangOptions::defaultIntWidth()), so a variable of
it lowers to i16, not the i64 every ISO 7185/EP ordinal always has.
BuiltinIO::emitWriteValue's dispatch was a closed if/else-if chain testing
isIntegerTy(64)/isDoubleTy/isBool/isIntegerTy(8), with anything else falling
to the STRING writer -- passing a bare i16 there is an LLVM IR
verifier abort ("Call parameter type does not match function signature"),
not a silently wrong answer.  This and its field-width sibling,
emitWriteValueFormatted, both needed a widening arm for any other integer
width before the dispatch, sign- or zero-extending per the value's own
Sema-type signedness.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:32767
CHECK-NEXT:   32767
CHECK-NEXT:-1234
CHECK-NEXT:   -1234
*)

program p;
var i: Integer;
begin
  i := 32767;
  writeln(i);
  writeln(i:8);
  i := -1234;
  writeln(i);
  writeln(i:8)
end.
