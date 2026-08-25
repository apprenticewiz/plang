(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 7 8 9
*)

program p(output);
type arr = array [1..3] of integer;
var v: arr; i: integer;
function q: arr; begin q := v end;
begin
  v[1] := 7; v[2] := 8; v[3] := 9;
  v := q;
  for i := 1 to 3 do write(v[i]:2); writeln
end.
