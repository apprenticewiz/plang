(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 12 13 
*)

program p(output);
var avf: array [1..3] of text;
    i, x: integer;
begin
  for i := 1 to 3 do begin
    rewrite(avf[i]); writeln(avf[i], i + 10) end;
  for i := 1 to 3 do begin
    reset(avf[i]); readln(avf[i], x); write(x:1, ' ') end;
  writeln
end.
