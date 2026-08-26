(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 4 9 16 
*)

program p;
var f: file of integer;
    i: integer;
begin
  rewrite(f);
  for i := 1 to 4 do begin f^ := i * i; put(f) end;
  reset(f);
  while not eof(f) do begin write(f^, ' '); get(f) end;
  writeln
end.
