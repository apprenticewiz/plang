(*
Tier 1 (`-std=turbo`) shipped checkedAdd/checkedSub/checkedMul/checkedNeg/
isoPow (Arith.h) as width-generic: pass the target type's Width/Signed and
a narrow (e.g. Turbo Integer, 16-bit) constant fold is bounds-checked
against ITS range instead of int64's.  Sema::constBoundImpl (SemaType.cpp)
and CodeGen's own ConstFold.cpp folder never actually passed that
argument, so every constant-expression fold used the 64-bit-signed
default regardless of dialect: `30000 + 30000` is 27233 past Turbo's
Integer range but nowhere near int64's, so this used to compile with ZERO
diagnostics and silently give Big the int64 result truncated to 16 bits
(-5536) at codegen time.

err_const_expr_out_of_range is the new diagnostic this now reaches. Every
OTHER caller of constBound (array/subrange bounds, ...) already has its
own "not a constant expression" diagnostic for the identical kind of
decline (see an-overflowing-array-bound-is-rejected-not-silently-wrapped.
pas for int64's own version of this); a plain `const` declaration is the
one context that had none, so it used to silently define a constant with
no known ordinal value at all instead of refusing the program.

RUN: not %plang_ir -std=turbo -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: constant expression is out of range for type 'integer' (must be between -32768 and 32767)
*)

program t;
const Big = 30000 + 30000;
begin
end.
