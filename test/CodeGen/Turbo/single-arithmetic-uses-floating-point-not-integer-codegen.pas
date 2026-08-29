(*
Regression guard for the exact bug this feature's own plan flagged as a
real bug to fix: CGBinaryOps::emitBinary's needFP test used to be
`lv->getType()->isDoubleTy() || rv->getType()->isDoubleTy()`, which is false
for two `float` (Single) operands -- neither is a double -- so a Single
addition fell through to the INTEGER arithmetic path below (plain
CreateAdd/CreateSub/CreateMul on float SSA values, which is either an LLVM
verifier abort or, were the verifier not to catch it, silently wrong
arithmetic on the operands' raw bit patterns).  needFP now tests
isFloatingPointTy(), true for float and double alike, so a Single operand
takes the promote-compute-narrow path below instead -- fpext to double
(numericResult, SemaExpr.cpp, always answers a Real-kind '+' with the wider
Real regardless of whether either operand was the narrower Single), fadd at
double precision, then fptrunc back to float when the result is stored into
a Single destination.  Unary minus has no such widening (there is only one
operand to agree with), so `-s1` stays a plain `fneg float`.  Checked
directly in the IR here, since single-declares-assigns-computes-and-
writes.pas (this directory) already proves the visible BEHAVIOR is correct
but a codegen path this wrong would most likely crash rather than misprint,
and a crash is exactly what this file's own plain (non-`not`) RUN line would
also catch; the IR-level check pins down *why* it doesn't, not just that it
doesn't.
*)

(*
RUN: %plang_ir -std=turbo -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

(*
CHECK: fpext float
CHECK-SAME: to double
CHECK: fpext float
CHECK-SAME: to double
CHECK: fadd double
CHECK: fptrunc double
CHECK-SAME: to float
CHECK: fneg float
CHECK-NOT: add i
*)

var
  s1, s2, s3: Single;
begin
  s1 := 1.5;
  s2 := 2.25;
  s3 := s1 + s2;
  s3 := -s1;
end.
