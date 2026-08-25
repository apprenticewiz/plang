(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true
CHECK-NEXT:true false
*)

program p(output);
type letter = 'a'..'z';
var b: letter; h: char;
begin b := 'm'; h := 'm'; writeln(b = h);
      h := 'z'; writeln(b < h, ' ', b > h) end.
