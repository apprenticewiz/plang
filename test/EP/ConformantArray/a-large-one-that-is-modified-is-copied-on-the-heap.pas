(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7777 1
*)

program p(output);
type big = array[1..200000] of integer;
var a: big; i: integer;
function clobber(k: integer;
                 x: array[lo..hi: integer] of integer): integer;
begin x[lo] := 7777;
  if k = 0 then clobber := x[lo] else clobber := clobber(k - 1, x) end;
begin
  for i := 1 to 200000 do a[i] := i;
  writeln(clobber(20, a), ' ', a[1])
end.
