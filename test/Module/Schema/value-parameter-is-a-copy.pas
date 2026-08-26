(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
*)

program p;
type vec(n: integer) = array[1..n] of integer;
var a: vec(3); i: integer;
procedure clobber(v: vec);
begin v[1] := 999 end;
begin
  for i := 1 to 3 do a[i] := i;
  clobber(a);
  writeln(a[1])
end.
