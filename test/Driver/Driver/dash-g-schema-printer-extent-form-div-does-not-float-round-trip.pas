(*
Issue #415.  plang_schema_printers.py's own _eval_form had its own
independent Python re-implementation of extent-form `div`
(`int(a / b)`) -- a float round-trip -- the exact same class of bug
issue #394 just fixed for the sibling `mod` branch in the same function
(_iso_mod).  For a discriminant-derived operand beyond 2**53, converting
it to a Python float loses precision, so the printer can compute a
DIFFERENT value than the compiled program's exact 64-bit `CreateSDiv` in
SchemaLayoutEngine::emitExtentForm.

a = 2**60 - 1 = 1152921504606846975, b = 2**59 = 576460752303423488:
the exact truncating quotient a div b is 1 ((2**60 - 1) is just under
2 * 2**59).  But float(a) is not exactly representable -- at that
magnitude a double's ULP is 128, and a sits only 1 below the next
representable value, 2**60 itself, so it rounds UP to exactly 2**60.
2**60 / 2**59 is then an exact 2.0, and int(2.0) is 2: the buggy printer
computes a bound one too many, silently reading a third, un-allocated
array word as if it were a real array element -- a value the compiled
program never actually computed (the compiler's own CreateSDiv, done in
exact 64-bit integer arithmetic with no float in sight, gives 1).
*)

(*
REQUIRES: gdb-binary
*)

(*
RUN: %plang -g -std=iso10206 %s -o %t
RUN: gdb -q -batch -ex "source %plang_schema_printers" -ex "break %s:40" -ex run -ex "print q^" %t 2>&1 | FileCheck %s
*)

program p;
type Vec(n: integer) = record a: array[0 .. n div 576460752303423488] of integer end;
type VecPtr = ^Vec;
var q: VecPtr;
begin
  new(q, 1152921504606846975);
  q^.a[0] := -1;
  q^.a[1] := 99;
  writeln(q^.a[1])
end.

(*
The check is anchored tightly on purpose, the same discipline #394's own
regression test uses: the field must be the exact 2-element array
holding -1 then 99, not merely CONTAIN 99 among other (garbage)
elements -- the unfixed printer instead shows a 3-element array whose
middle element is 99 with a garbage word following it, which a looser
"contains 99 somewhere" check would pass just as well, catching
nothing. The END
capture below requires the character right after 99 to NOT be a comma:
the unfixed printer's next character there is a comma, going on to its
extra element; the fixed printer's is the array's own closing delimiter
instead. No literal curly-brace character (the array's own delimiter)
appears anywhere in this comment, on purpose -- ISO Pascal accepts that
character as an alternate close for a comment opened with a
left-parenthesis-asterisk too, so one here would end this very comment
early.
CHECK: Vec = [[SEP1:.*]]n = 1152921504606846975, a = [[SEP2:.*]]-1, 99[[END:[^,]]]
*)
