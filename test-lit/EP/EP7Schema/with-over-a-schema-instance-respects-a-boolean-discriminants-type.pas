(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true
*)

program p;
type Box(c: boolean) = record x: integer end;
var b: Box(true);
begin
  with b do
    if c then writeln('true') else writeln('false')
end.
