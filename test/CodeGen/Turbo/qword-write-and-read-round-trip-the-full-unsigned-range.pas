(*
QWord (Turbo's 64-bit unsigned integer) is the one ordinal a signed i64
formatter/parser gets wrong: a value past INT64_MAX is a legitimate QWord
value, but plang_write_i64's "%" PRId64 prints it as negative, and
plang_read_i64's strtoll ERANGEs trying to parse it back.  Every narrower
unsigned rung (Byte/Word/Cardinal/LongWord) is zero-extended to i64 before
ever reaching a writer, so it never sets the i64 sign bit and the signed and
unsigned writers agree byte for byte -- QWord is the only rung wide enough to
disagree, so it needs its own %PRIu64-based write and strtoull-based read
entry points (plang_write_u64/plang_read_u64 and their _turbo/_file twins),
and CodeGen has to actually route a QWord destination to them instead of the
plain _i64/_f64 family.

No QWord literal appears directly in source: an integer literal past
INT64_MAX is rejected by the lexer's own int64 range check regardless of the
destination type ("integer literal '...' is out of range" -- a separate,
known limitation of literal parsing, not of QWord's write/read path this
test is about), so every boundary value here is built with arithmetic a
QWord variable can hold: QWord(-1) for the maximum (an all-ones bit pattern,
constructed the same way `fpc -Mtp` requires -- there is no unsigned literal
syntax either place), and QWord(9223372036854775807) + 1 -- Int64's own
maximum, itself in range for a plain literal -- for the value one past it
(2^63), which sets the i64 sign bit but is otherwise an ordinary small-
magnitude-relative-to-QWord's-range value.  The same two values are then
read back from stdin, in full-range decimal, to exercise
plang_read_u64_turbo.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:18446744073709551615
CHECK-NEXT:9223372036854775808
CHECK-NEXT:read: 18446744073709551615
CHECK-NEXT:read: 9223372036854775808
*)

//--- test.pas
var
  qMax, qMid, qRead: QWord;
begin
  qMax := QWord(-1);
  writeln(qMax);
  qMid := QWord(9223372036854775807) + 1;
  writeln(qMid);

  readln(qRead);
  writeln('read: ', qRead);
  readln(qRead);
  writeln('read: ', qRead);
end.

//--- stdin.txt
18446744073709551615
9223372036854775808
