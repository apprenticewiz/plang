(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:****.
*)

program p(output);
type stars = array [1..5] of char value [1..4: '*'; otherwise '.'];
var s: stars; i: integer;
begin for i := 1 to 5 do write(s[i]); writeln end.
