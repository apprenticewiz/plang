(*
CGDebugInfo::debugTypeOfSemaType's TypeKind::Integer case used to hardcode
createBasicType("integer", 64, DW_ATE_signed) regardless of the Sema Type's
own Width/IsSigned -- correct for ISO 7185 and Extended Pascal, where every
Integer really is Width=64 (see dash-g-gives-each-scalar-kind-its-own-d-i-
type.pas, unaffected by this fix), but wrong for Turbo, whose Integer has
been a real 16-bit signed type since Tier 1 shipped
(LangOptions::defaultIntWidth()).  The LLVM value itself was always i16 --
CGTypes::llvmTypeOfSemaTypeImpl already reads T.Width -- only the debug-info
side lied, so a debugger reading DW_AT_byte_size read 8 bytes of memory for
a 2-byte value.  See the companion gdb-driven test in this same directory
for the effect that had on an actual debugger session.
*)

(*
RUN: %plang_ir -std=turbo -g -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p;
var i: Integer;
begin
  i := -1234;
  writeln(i)
end.

(*
CHECK: !DIBasicType(name: "integer", size: 16, encoding: DW_ATE_signed)
*)
