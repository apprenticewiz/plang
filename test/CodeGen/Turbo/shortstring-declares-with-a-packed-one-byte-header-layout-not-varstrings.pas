(*
The layout requirement this whole feature exists to get right: Turbo
string[N] (TypeKind::ShortString) must lower to a PACKED <{ i8, [N x i8] }>
-- a ONE-BYTE length prefix, 1-byte alignment, SizeOf(string[N]) == N+1 --
and must NOT reuse Extended Pascal string(N)'s (TypeKind::VarString) struct
or its struct-type cache (CGTypes::strStructTypes_ is keyed on capacity
alone, an int64_t, with no way to tell an EP capacity from a Turbo one
apart -- see CGTypes::sstrStructType's own comment for why reusing it would
be a serious, silent layout-corruption bug rather than a mere naming
collision).  Checked directly against the emitted LLVM IR, the same way
test/CodeGen/Storage/an-unpacked-record-is-still-padded.pas checks a
record's packedness.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -emit-llvm %t.dir/test.pas -o %t.ll
RUN: FileCheck %s < %t.ll
*)

(*
CHECK-NOT: { i64, [10 x i8] }
CHECK: @pasg_s = global <{ i8, [10 x i8] }> zeroinitializer
*)

//--- test.pas
program p;
var s: string[10];
begin
  s := s;
  writeln('ok')
end.
