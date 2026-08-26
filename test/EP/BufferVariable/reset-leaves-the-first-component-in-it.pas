(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 1 2
*)

program p;
var f: file of integer;
    i, v: integer;
begin
  rewrite(f);
  for i := 1 to 3 do begin f^ := i; put(f) end;
  reset(f);
  v := f^;
  read(f, i);
  writeln(v, ' ', i, ' ', f^)
end.
