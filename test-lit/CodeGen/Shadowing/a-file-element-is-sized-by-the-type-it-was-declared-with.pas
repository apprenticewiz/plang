(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5 10 15 20 25 30 35 40 45 50 
*)

program p(output);
type t = array[1..10] of integer;
var f: file of t; x, y: t; i: integer;
procedure inner;
type t = array[1..2] of integer;
var l: t; i: integer;
begin
  l[1] := 0;
  rewrite(f);
  for i := 1 to 10 do x[i] := i * 5;
  f^ := x; put(f)
end;
begin
  inner;
  reset(f); y := f^;
  for i := 1 to 10 do write(y[i]:1, ' ');
  writeln
end.
