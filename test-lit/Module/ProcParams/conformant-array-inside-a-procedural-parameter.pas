(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:123
CHECK-NEXT:12345
*)

program p;
procedure show(a: array[lo..hi: integer] of integer);
var i: integer;
begin for i := lo to hi do write(a[i]:1); writeln end;
procedure run(procedure s(a: array[u..v: integer] of integer));
var small: array[1..3] of integer;
    big:   array[1..5] of integer;
    i: integer;
begin
  for i := 1 to 3 do small[i] := i;
  for i := 1 to 5 do big[i] := i;
  s(small); s(big)
end;
begin run(show) end.
