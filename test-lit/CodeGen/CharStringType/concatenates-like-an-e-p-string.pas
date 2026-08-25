(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:abcde
CHECK-NEXT:abcX
CHECK-NEXT:Yde
CHECK-NEXT:abcde 5
*)

program p(output);
var a: packed array[1..3] of char; b: packed array[1..2] of char;
    s: string(20);
begin a := 'abc'; b := 'de';
  writeln(a + b);
  writeln(a + 'X');
  writeln('Y' + b);
  s := a + b; writeln(s, ' ', length(s):1) end.
