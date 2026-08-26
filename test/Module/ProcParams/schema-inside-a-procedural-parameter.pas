(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:456
*)

program p;
type vec(n: integer) = array[1..n] of integer;
procedure show(v: vec);
var i: integer;
begin for i := 1 to 3 do write(v[i]:1); writeln end;
procedure run(procedure s(v: vec));
var a: vec(3);
begin a[1] := 4; a[2] := 5; a[3] := 6; s(a) end;
begin run(show) end.
