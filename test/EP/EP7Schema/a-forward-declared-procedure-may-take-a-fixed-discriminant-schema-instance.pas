(*
A fixed-discriminant schema instance (Vec(5)) is written out afresh at every
occurrence rather than interned, so a forward declaration's parameter type
and its matching definition's parameter type were two distinct Type objects
even though they denote the same instantiation.  sameParamType fell through
to pointer-identity comparison for SchemaInstance, so the identical heading
written twice was rejected as not congruous with itself.
*)

(*
RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:15
*)

program p;
type Vec(n: integer) = record
  data: array[1..n] of integer
end;
procedure sumInto(v: Vec(5); var total: integer); forward;
procedure sumInto(v: Vec(5); var total: integer);
var i: integer;
begin
  total := 0;
  for i := 1 to 5 do total := total + v.data[i]
end;
var v: Vec(5); i: integer; t: integer;
begin
  for i := 1 to 5 do v.data[i] := i;
  sumInto(v, t);
  writeln(t)
end.
