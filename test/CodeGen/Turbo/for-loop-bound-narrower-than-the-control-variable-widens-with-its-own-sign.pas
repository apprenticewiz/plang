(*
CGControlFlow::emitFor coerces From/Limit into the control variable's own
storage type through CoerceToType with no signedness argument -- the same
pre-ladder "guess from LLVM width" fallback CGAssign's plain-assignment
path used to fall into (see shortint-assigned-to-a-wider-signed-integer-
sign-extends-issue-177.pas).  This is a DIFFERENT bug from the one
for-loop-bound-over-unsigned-sized-integer-type-covers-the-full-range.pas
already covers (that one is about choosing a signed vs. unsigned ICMP for
the loop condition, once From/Limit are already the control variable's
own width): this one is about the WIDENING into that width in the first
place, which runs first.  An unsigned Word bound assigned to a wider
signed LongInt control variable had its own zero-extended-by-width-guess
bit pattern -- wrong only when the guess disagrees with the bound's real
type, as it does for Word -- sign-extended into a large NEGATIVE starting
value instead of the true positive one (issue #177's sibling audit).
Fixed by consulting From/Limit's own Sema-resolved signedness
(exprIsSigned, OrdinalSignedness.h).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
*)

program p;
var
  w: Word;
  i: LongInt;
  count: LongInt;
begin
  w := 65533;
  count := 0;
  for i := w to 65535 do
    count := count + 1;
  writeln(count)
end.
