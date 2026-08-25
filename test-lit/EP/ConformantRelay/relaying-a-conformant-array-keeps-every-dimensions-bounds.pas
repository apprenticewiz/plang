(*
RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1..2 3..7
CHECK-NEXT:13 14 15 16 17 
CHECK-NEXT:23 24 25 26 27 
CHECK-NEXT:1..2 3..7
CHECK-NEXT:13 14 15 16 17 
CHECK-NEXT:23 24 25 26 27 
*)

program p(output);
type mat = array[1..2, 3..7] of integer;
var m: mat; i, j: integer;
procedure show(var a: array[u..w: integer; lo..hi: integer] of integer);
var r, c: integer;
begin
  writeln(u, '..', w, ' ', lo, '..', hi);
  for r := u to w do begin
    for c := lo to hi do write(a[r, c], ' ');
    writeln
  end
end;
procedure relay(var a: array[u..w: integer; lo..hi: integer] of integer);
begin show(a) end;
begin
  for i := 1 to 2 do for j := 3 to 7 do m[i, j] := i * 10 + j;
  show(m); relay(m)
end.
