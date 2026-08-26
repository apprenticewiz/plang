(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3: 1 2 3
CHECK-NEXT:5: 10 20 30 40 50
*)

program p;
type vec(n: integer) = array[1..n] of integer;
var a: vec(3); b: vec(5); i: integer;
procedure show(var v: vec);
var j: integer;
begin
  write(v.n, ':');
  for j := 1 to v.n do write(' ', v[j]);
  writeln
end;
begin
  for i := 1 to 3 do a[i] := i;
  for i := 1 to 5 do b[i] := i * 10;
  show(a); show(b)
end.
