(*
Issue #633: Boolean(x) as a VALUE CAST (not a type name -- the cast
expression) routed through emitTypeCastValue's generic scalar-conversion
path, which for a strict Boolean destination (always i1,
CGTypes::llvmTypeOfSemaTypeImpl) is a plain CoerceToType *ExtOrTrunc --
truncation to i1 keeps only the LOW BIT of an integer source, so
Boolean(2) and Boolean(4) (both even) came out False.

Real TP7/fpc field practice -- confirmed against `fpc -Mtp` (3.2.2) --
treats ANY nonzero value as True on a Boolean value cast, exactly like
Turbo's loose ByteBool/WordBool/LongBool family already does
(`ByteBool(200)` is True): fpc's Boolean(0)..Boolean(5) loop prints
1..5 all true-ish, not just the odd ones.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 is true-ish
CHECK-NEXT:2 is true-ish
CHECK-NEXT:3 is true-ish
CHECK-NEXT:4 is true-ish
CHECK-NEXT:5 is true-ish
*)

program boolean_cast_nonzero;
var
  i: Integer;
begin
  for i := 0 to 5 do
    if Boolean(i) then writeln(i, ' is true-ish');
end.
