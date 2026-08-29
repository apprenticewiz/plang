(*
The width threading in constBoundImpl (SemaType.cpp) covers every checked-
arithmetic call site, not just the binary +/-/* used by the sibling
turbo-integer-const-arithmetic-overflow-is-rejected.pas: unary minus, abs,
sqr, succ and pred all route through checkedNeg/checkedMul/checkedAdd/
checkedSub the same way.  sqr(200) = 40000 is comfortably inside int64 but
6465 past Turbo's 16-bit Integer range, so it is refused here the same
way the binary-operator case is -- proving the fix is not narrowly special-
cased to '+'.

RUN: not %plang_ir -std=turbo -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: constant expression is out of range for type 'integer' (must be between -32768 and 32767)
*)

program t;
const Big = sqr(200);
begin
end.
