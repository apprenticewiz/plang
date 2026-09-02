(*
Issue #691: a LOCAL (procedure-body) variable's own discriminant-value list
need not be a compile-time constant -- ISO 10206 §6.4.8 evaluates
discriminant-values "within the commencement of an activation of a block",
i.e. once per activation, exactly like an ordinary local's own initializer.
plang's heap path (`new(p, k)`) already accepted a run-time discriminant;
this used to be rejected for a local variable's own declaration
(`var v: vec(k);`) with "must be a constant expression" even though k is
just an ordinary formal parameter.

sumv (a schema-typed VALUE parameter, itself already run-time-discriminated
machinery) exercises v as an ordinary schema value once built, confirming
the local's own run-time-sized storage is laid out and readable exactly
like any other schema instance.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p(output);
type vec(n: integer) = record
  data: array[1..n] of integer
end;

procedure sumv(v: vec);
var i, s: integer;
begin
  s := 0;
  for i := 1 to v.n do s := s + v.data[i];
  writeln(s:0)
end;

procedure f(k: integer);
var v: vec(k); i: integer;
begin
  for i := 1 to k do v.data[i] := i;
  sumv(v)
end;

begin
  f(4);
  f(6)
end.

(*
CHECK:10
CHECK-NEXT:21
*)
