(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:abcde
CHECK-NEXT:abcde....
*)

program p(output);
var u: array [1..5] of char; z: packed array [1..5] of char;
    wide: array [1..9] of char; i: integer;
procedure topacked(var x: array [lo..hi: integer] of char);
begin pack(x, lo, z) end;
procedure fromacked(var x: array [lo..hi: integer] of char);
begin unpack(z, x, lo) end;
begin
  u[1] := 'a'; u[2] := 'b'; u[3] := 'c'; u[4] := 'd'; u[5] := 'e';
  topacked(u); writeln(z);
  for i := 1 to 9 do wide[i] := '.';
  fromacked(wide);
  for i := 1 to 9 do write(wide[i]); writeln
end.
