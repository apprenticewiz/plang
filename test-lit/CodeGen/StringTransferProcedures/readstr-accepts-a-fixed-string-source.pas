(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0.0 - 4
*)

program p(output);
var e: packed array[1..8] of char; r: real; c: char; i: integer;
begin e := '0.0-4   '; readstr(e, r, c, i);
  writeln(r:3:1, ' ', c, ' ', i:1) end.
