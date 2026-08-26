(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true branch
*)

program p;
type Box(c: boolean) = record x: integer end;
var b: ^Box;
begin
  new(b, true);
  if b^.c then writeln('true branch') else writeln('false branch')
end.
