(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:101
*)

program p;
var counter: integer value 100;
begin
  counter := counter + 1;
  writeln(counter)
end.
