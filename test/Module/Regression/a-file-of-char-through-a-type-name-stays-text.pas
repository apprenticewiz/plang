(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hi 
*)

program p(output);
type chfile = file of char;
var f: chfile; c: char;
begin
  rewrite(f); write(f, 'h'); write(f, 'i');
  reset(f);
  while not eof(f) do begin read(f, c); write(c) end;
  writeln
end.
