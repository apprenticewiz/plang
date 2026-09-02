(*
Issue #591: a record with a scalar field (whose own end offset is NOT
already a multiple of 8) immediately before a Turbo 'string[N]' field used
to crash the compiler outright (fatal `codegenICE`, no .o produced) --
SchemaLayoutEngine's run-time layout walk (rtAlignOfTypeNode) answered 8
for EVERY StringTypeNode, EP's own string(cap) AND Turbo's string[N]
alike, so it padded the preceding scalar field's offset up to a multiple
of 8 before laying out the string[N] field. The STATIC LLVM layout
(layoutOfRecord/llvmTypeOfNode) already used alignment 1 for a Turbo
ShortString and never padded at all, so the two walks disagreed and
checkFieldOffsetAgreement's cross-check (correctly) aborted the process.

Confirmed against a local `fpc -Mtp` build: no padding at all -- SVal
starts immediately after Tag, at whatever odd offset Tag's own width
leaves it.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:24
CHECK-NEXT:hi
*)

program scalar_before_shortstring;
type
  R = record
    Tag: Integer;
    SVal: string[20];
  end;
var
  V: R;
begin
  V.Tag := 0;
  V.SVal := 'hi';
  writeln(SizeOf(R));
  writeln(V.SVal);
end.
