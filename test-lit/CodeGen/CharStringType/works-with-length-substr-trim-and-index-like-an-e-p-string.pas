(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
CHECK-NEXT:ell
CHECK-NEXT:3
CHECK-NEXT:[hello]
*)

program p(output);
var a: packed array[1..5] of char;
begin a := 'hello';
  writeln(length(a):1);
  writeln(substr(a, 2, 3));
  writeln(index(a, 'll'):1);
  writeln('[', trim(a), ']') end.
