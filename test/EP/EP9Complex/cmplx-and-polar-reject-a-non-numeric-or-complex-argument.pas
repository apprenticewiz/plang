(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

{ EP §6.7.6.3: cmplx(x,y) and polar(r,t) each COMBINE two real-type
  components into a complex result ("the expressions x and y [/ r and t]
  that shall be of real-type" -- no complex alternative, unlike Table 2's
  footnote (1) for sqrt/sin/...), so each argument has to be numeric and,
  unlike that sibling arm, never complex: a nested complex value has no
  meaning as one component of a NEW complex value.  Neither argument was
  checked at all before this (issue #306): `cmplx(true, 'x')` type-checked
  with nothing to reject it, and CodeGen's ToDouble (CGFuncCall.cpp) has no
  lowering for a non-numeric or complex-shaped argument either. }

program p;
var
  b: boolean;
  c: complex;
begin
  c := cmplx(1.0, b);
  c := polar(c, 1.0)
end.

(*
ERR: 'cmplx' requires a numeric argument, got 'boolean'
ERR: 'polar' requires a numeric argument, got 'complex'
*)
