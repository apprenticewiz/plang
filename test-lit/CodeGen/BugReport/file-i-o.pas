(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hello 
*)

program tfile;
var f: text; ch: char;
begin
  rewrite(f);
  writeln(f, 'hello');
  reset(f);
  while not eof(f) do begin read(f, ch); write(ch) end;
  writeln
end.
