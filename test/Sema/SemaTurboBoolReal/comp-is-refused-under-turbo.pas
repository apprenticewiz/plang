(*
Comp (64-bit fixed-point "currency") is Extended's sibling refusal -- see
extended-is-refused-under-turbo.pas (this directory) for the full reasoning;
this file exists so both names covered by err_turbo_unsupported_float_type
have their own regression test rather than one standing in for the other.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'Comp' is not a supported plang type; plang implements Turbo's Real and Single floating-point types but not Extended or Comp
*)

var
  x: Comp;
begin
end.
