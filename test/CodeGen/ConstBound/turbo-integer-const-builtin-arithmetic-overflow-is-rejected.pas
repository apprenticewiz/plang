(*
The width threading in constBoundImpl (SemaType.cpp) covers every checked-
arithmetic call site, not just the binary +/-/* used by the sibling
turbo-integer-const-arithmetic-overflow-is-rejected.pas: unary minus, abs,
sqr, succ and pred all route through checkedNeg/checkedMul/checkedAdd/
checkedSub the same way.

issue #609 UPDATE: this test used to fold `sqr(200)` (= 40000) against
Turbo's 16-bit Integer range and reject it as 6465 past that range -- but
that assumed Sqr's OWN result type stayed at its narrow ShortInt/Byte/
Integer/Word ARGUMENT's width, which #609 found does not match real
`fpc -Mtp`: any Turbo integer argument narrower than 32 bits promotes
Abs/Sqr to 32-bit SIGNED (LongInt) instead (Sema/SemaExpr.cpp's "abs/sqr
are polymorphic" arm has the full derivation).  `const Big = sqr(200);`
is Cd.Value's own checkExpr-resolved type (LongInt, post-#609) that
constBoundImpl's overflow check runs against, and 40000 fits LongInt's
range comfortably -- so this specific repro no longer demonstrates an
overflow at all (confirmed it compiles clean now; see
test/CodeGen/Turbo/abs-and-sqr-promote-a-narrow-integer-argument-past-its-
own-width-issue-609.pas for that new, correct behavior in isolation).
Swapped in sqr(50000) = 2500000000, which overflows even the CORRECTLY
promoted 32-bit-signed LongInt result (max 2147483647), to keep
demonstrating that Sqr's const fold is still checked against its own
result type rather than silently accepted -- plang's const-fold
architecture deliberately does not auto-widen further to fit an
overflowing value the way real fpc's own (different) "smallest type for
the folded value" constant-typing rule would (the unmodified sibling test
`30000 + 30000` already establishes that architecture; real fpc happens
to accept `sqr(50000)` too, inferring a wider Cardinal-shaped constant --
not something this test's own reject-on-overflow contract has ever tried
to match).

RUN: not %plang_ir -std=turbo -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: constant expression is out of range for type 'LongInt' (must be between -2147483648 and 2147483647)
*)

program t;
const Big = sqr(50000);
begin
end.
