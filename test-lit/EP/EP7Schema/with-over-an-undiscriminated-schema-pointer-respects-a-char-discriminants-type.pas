(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:A
*)

program p;
type Box(c: char) = record x: integer end;
var b: ^Box;
begin
  new(b, 'A');
  with b^ do
    if c = 'A' then writeln('A')
end.
