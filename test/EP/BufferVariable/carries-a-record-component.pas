(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7x 8y 
*)

program p;
type rec = record a: integer; b: char end;
var f: file of rec;
    r: rec;
begin
  rewrite(f);
  r.a := 7; r.b := 'x'; f^ := r; put(f);
  r.a := 8; r.b := 'y'; f^ := r; put(f);
  reset(f);
  while not eof(f) do begin write(f^.a, f^.b, ' '); get(f) end;
  writeln
end.
