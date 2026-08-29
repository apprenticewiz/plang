(*
A for-loop's condition (CGControlFlow::emitFor) needs the same signed/
unsigned icmp choice a relational operator does (see
test/CodeGen/Turbo/unsigned-sized-integer-types-compare-unsigned.pas) --
but for a REASON its own bounds' types cannot always answer: `From`/`Limit`
are coerced into the control variable's own storage before the comparison
ever runs, so what the comparison must respect is that storage's
signedness, not either bound EXPRESSION's own pre-coercion one.

A bound that is itself a variable (the second `for` below) is ordinarily
assignment-compatible with -- and so shares the signedness of -- the control
variable it bounds.  A bare integer LITERAL bound (the first `for`) is not:
it is always typed as the dialect's plain signed Integer regardless of its
value, so `for w := 0 to 65535 do` with `w: Word` used to see two signed-
Integer-typed bounds, pick a signed compare, read 65535 (truncated into
Word's 16 bits, the all-ones bit pattern) as -1, find `0 <= -1` false, and
run the loop zero times -- both wrong, and unlike the relational-operator
case, silently: no crash, no wrong-looking output, just a body that never
ran.  See ForStmt::VarType (AstStmt.h) and Sema::checkFor's own comments.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:65536
CHECK-NEXT:65536
*)

program p;
var
  w, lo, hi: Word;
  count: LongInt;
begin
  count := 0;
  for w := 0 to 65535 do
    count := count + 1;
  writeln(count);

  lo := 0; hi := 65535;
  count := 0;
  for w := lo to hi do
    count := count + 1;
  writeln(count)
end.
