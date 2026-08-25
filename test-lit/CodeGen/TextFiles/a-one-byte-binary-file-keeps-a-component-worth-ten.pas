(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:8 9 10 11 12 
*)

program p(output);
type small = 0..255;
var f: file of small; v: small; i: integer;
begin
  rewrite(f);
  for i := 8 to 12 do begin f^ := i; put(f) end;
  reset(f);
  while not eof(f) do begin v := f^; get(f); write(v:1, ' ') end;
  writeln
end.
