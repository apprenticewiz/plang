(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:done
*)

program p(output);
type rec = record i: integer end;
var i: integer; r: rec;
begin
  for i := 1 to 3 do with r do i := 5;
  writeln('done')
end.
