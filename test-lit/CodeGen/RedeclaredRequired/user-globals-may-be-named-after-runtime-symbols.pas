(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
*)

program p(output);
var date, time, position, binding: integer;
begin
  date := 1; time := 2; position := 3; binding := 4;
  writeln(date + time + position + binding)
end.
