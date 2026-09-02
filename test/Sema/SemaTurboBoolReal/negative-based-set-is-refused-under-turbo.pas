(*
Issue #692: -std=turbo used to accept a negative-based set (`set of -5..5`),
checking only the WINDOW's element count (checkSetBaseRange, SemaType.cpp)
and not its sign.  Real `fpc -Mtp` 3.2.2 refuses it outright ("illegal type
declaration of set elements") even though -5..5 is only 11 elements, well
under the 256-element limit: a TP7 set base's own ordinals must be 0..255,
unlike EP's own sets (ISO/IEC 10206), which DO allow a negative base -- see
test/CodeGen/CodegenSets/negative-base-*.pas, which already exercise that
under the default (non-Turbo) dialect and are untouched by this fix.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: set base type '-5..5' must not have a negative lower bound in Turbo Pascal
*)

type
  SB = set of -5..5;
begin
end.
