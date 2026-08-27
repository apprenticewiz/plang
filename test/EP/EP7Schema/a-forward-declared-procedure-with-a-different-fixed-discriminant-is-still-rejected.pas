(*
Structural comparison of schema-instance parameter types must not weaken
into accepting a genuinely different instantiation: Vec(5) and Vec(6) are
still incongruous, and the forward declaration mismatch must still be
diagnosed.
*)

(*
RUN: not %plang_ep %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: does not match forward declaration
*)

program p;
type Vec(n: integer) = record
  data: array[1..n] of integer
end;
procedure sumInto(v: Vec(5); var total: integer); forward;
procedure sumInto(v: Vec(6); var total: integer);
var i: integer;
begin
  total := 0;
  for i := 1 to 6 do total := total + v.data[i]
end;
var v: Vec(6); i: integer; t: integer;
begin
  for i := 1 to 6 do v.data[i] := i;
  sumInto(v, t);
  writeln(t)
end.
