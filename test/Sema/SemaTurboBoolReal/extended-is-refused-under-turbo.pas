(*
Extended (80-bit) is a real Turbo Pascal type name plang recognizes but does
not implement -- only Real (64-bit) and, alongside it, Single (32-bit) have
an LLVM lowering (CGTypes::llvmTypeOfSemaTypeImpl's Real case).  Naming it
gets an explicit, dialect-aware refusal (resolveNamedUnrestricted,
SemaType.cpp) instead of the generic "undefined type" a misspelling would,
which is not the mistake being made here.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'Extended' is not a supported plang type; plang implements Turbo's Real and Single floating-point types but not Extended or Comp
*)

var
  x: Extended;
begin
end.
