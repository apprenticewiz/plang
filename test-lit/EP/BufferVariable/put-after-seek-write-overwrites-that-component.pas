(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:99 2 3 4 
*)

program p;
var f: file[1..10] of integer;
    i: integer;
begin
  rewrite(f);
  for i := 1 to 4 do write(f, i);
  seekwrite(f, 1); f^ := 99; put(f);
  seekread(f, 1);
  for i := 1 to 4 do begin write(f^, ' '); get(f) end;
  writeln
end.
