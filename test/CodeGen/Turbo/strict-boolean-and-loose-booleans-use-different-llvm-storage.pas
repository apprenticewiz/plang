(*
Regression guard for the exact bug this feature's own plan called out by
name: llvmTypeOfSemaTypeImpl's Boolean case used to return i1Ty
unconditionally, and an i1 cannot hold a value like ByteBool(200).  Strict
Boolean must stay i1 -- byte-identical to every dialect's Boolean before
this feature existed -- while ByteBool/WordBool/LongBool (Type::IsLooseBool)
get real i8/i16/i32 storage wide enough for any bit pattern.  Checked
directly in the IR rather than only through behavior (loose-booleans-read-
any-nonzero-value-as-true.pas, this directory), since a storage-width
regression here could easily still happen to print the right answer for
small values while corrupting adjacent memory for anything that does not
fit -- i1 storage plus an out-of-range store is exactly the kind of bug
that would not show up by accident in a single value's output.

RUN: %plang_ir -std=turbo -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

(*
CHECK-DAG: @pasg_strictB = global i1
CHECK-DAG: @pasg_bb = global i8
CHECK-DAG: @pasg_wb = global i16
CHECK-DAG: @pasg_lb = global i32
*)

var
  strictB: Boolean;
  bb: ByteBool;
  wb: WordBool;
  lb: LongBool;
begin
  strictB := true;
  bb := false;
  wb := false;
  lb := false;
end.
