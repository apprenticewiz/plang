(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hello
CHECK-NEXT:   hello
CHECK-NEXT:[hello]
*)

program p(output);
var s, t: packed array[1..5] of char;
begin s := 'hello'; t := s;
  writeln(t); writeln(t:8); writeln('[', t, ']') end.
