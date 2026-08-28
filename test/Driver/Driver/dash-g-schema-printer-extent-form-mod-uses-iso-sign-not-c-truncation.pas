(*
Issue #394.  plang_schema_printers.py's own _eval_form had its own
independent Python re-implementation of extent-form `mod`
(`a - b * int(a / b)`) -- a C-truncating remainder (sign of the DIVIDEND,
matching C's `%` / LLVM's `srem`) instead of ISO 7185 Section 6.7.2.2's
`mod` (result in [0, b), sign of the DIVISOR) -- the identical bug issue
#228 already fixed on the compiler side, in
SchemaLayoutEngine::emitExtentForm's own raw `srem` lowering.

`n mod 3 .. 2` with n = -4: ISO mod gives (-4) mod 3 = 2 ((-4) = (-2)*3 + 2),
so the compiler (fixed since #228) allocates array `a` as the 1-element
range 2..2. The unfixed printer instead recomputed the C-truncating
(-4) - 3 * int(-4/3) = -1, a 4-element range -1..2, and read three extra,
un-allocated words as if they were real array elements -- displaying a
value the compiled program never actually computed.
*)

(*
REQUIRES: gdb-binary
*)

(*
RUN: %plang -g -std=iso10206 %s -o %t
RUN: gdb -q -batch -ex "source %plang_schema_printers" -ex "break %s:34" -ex run -ex "print q^" %t 2>&1 | FileCheck %s
*)

program p;
type Vec(n: integer) = record a: array[n mod 3 .. 2] of integer end;
type VecPtr = ^Vec;
var q: VecPtr;
begin
  new(q, -4);
  q^.a[2] := 99;
  writeln(q^.a[2])
end.

(*
The check is anchored tightly on purpose: the field must be the exact
1-element array holding just 99, not merely CONTAIN 99 among other
(garbage) elements -- the unfixed printer instead shows a 4-element array
whose last element happens to be 99, with three garbage words ahead of it,
which a looser "contains 99 somewhere" check would pass just as well,
catching nothing. The END capture below requires the character right
after 99 to NOT be a comma: the unfixed printer's next character there is
a comma, going on to its extra elements; the fixed printer's is the
array's own closing delimiter instead. No literal curly-brace character
(the array's own delimiter) appears anywhere in this comment, on purpose
-- ISO Pascal accepts that character as an alternate close for a comment
opened with a left-parenthesis-asterisk too, so one here would end this
very comment early.
CHECK: Vec = [[SEP1:.*]]n = -4, a = [[SEP2:.*]]99[[END:[^,]]]
*)
