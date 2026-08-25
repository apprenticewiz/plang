(*
RUN: %plang -std=iso10206 -fno-range-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
*)

program p(output);
type Color = (red, green, blue); var c: Color;
begin c := blue; writeln(ord(succ(c))) end.
