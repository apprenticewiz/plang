(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0.0 - 4
*)

program p;
var E: string(20); R: real; C: char; I: integer;
begin
  E := '0.0-4';
  readstr(E, R, C, I);
  writeln(R:0:1, ' ', C, ' ', I)
end.
