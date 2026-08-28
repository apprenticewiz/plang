(*
Issue #408.  emitFieldLoad's SchemaInstance arm (EP §6.8.4 discriminant
access) hands back D.Value -- a Sema-time constant -- when the field being
read is a discriminant.  That is correct for a genuinely fixed instance
(a-schema-instance-discriminant-read-is-narrowed-to-its-declared-type.pas,
issue #210's own test: `var x: t('a')`), but D.Value is only ever a
PLACEHOLDER (the probe pass's stand-in, here the declared subrange's own low
bound) when the instance's discriminant is itself a FORM over an ENCLOSING
schema's discriminant: `outer(n) = record y: inner(n) end`.  Reading
`q^.y.m` printed 1 for every i, including i = 7 below, where the correct
answer -- inner's m, bound to outer's own n -- is 7.

Sibling to #393 (round 7), which fixed the analogous SIZING gap for this
exact shape: descendIntoInstantiation (SchemaAccess.cpp) already computes
inner's TRUE run-time discriminant from outer's own, through emitExtentForm
over the SchemaTypeNode's ActualForms, for sizing/offset purposes -- this is
the discriminant READ sibling, where emitFieldLoad never consulted it.
*)

(*
RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

program p(output);
type small = 1..10;
     inner(m: small) = record k: integer end;
     outer(n: integer) = record y: inner(n) end;
var q: ^outer; i: integer;
begin
  i := 7;
  new(q, i);
  writeln(q^.y.m)
end.
