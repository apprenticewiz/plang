(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2 3 4 5 
*)

program p(output);
type vec = array[1..5] of integer;
var a: vec; i: integer;
procedure clobber(x: array[lo..hi: integer] of integer);
var j: integer;
begin for j := lo to hi do x[j] := 99 end;
begin
  for i := 1 to 5 do a[i] := i;
  clobber(a);
  for i := 1 to 5 do write(a[i], ' ');
  writeln
end.
