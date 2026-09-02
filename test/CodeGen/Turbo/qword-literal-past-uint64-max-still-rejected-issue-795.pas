(*
Issue #795's parser fix retries an int64-overflowing literal as a uint64_t
before giving up (Parser::parseFactor, ParseExpr.cpp). A literal past even
UInt64::max (18446744073709551615) has no representation either way and
must still hit the original hard "out of range" rejection, at parse time,
regardless of destination -- this is the regression guard for that second
from_chars attempt not accidentally widening what gets accepted.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: integer literal '18446744073709551616' is out of range
*)

program qwordLiteralPastUInt64Max;
var
  q: QWord;
begin
  q := 18446744073709551616;
  writeln(q);
end.
