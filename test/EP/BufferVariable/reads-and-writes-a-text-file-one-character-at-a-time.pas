(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:ab 
*)

program p(output);
var f: text;
    c: char;
begin
  rewrite(f);
  f^ := 'a'; put(f);
  f^ := 'b'; put(f);
  reset(f);
  while not eof(f) do begin c := f^; write(c); get(f) end;
  writeln
end.
