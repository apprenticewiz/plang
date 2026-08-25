(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:q
CHECK-NEXT:h
CHECK-NEXT:z
*)

program p(output);
var c1, c2, c3: char; a: packed array[1..3] of char; s5: string(5);
begin
  a := 'q  '; c1 := a; writeln(c1);
  s5 := 'hello'; c2 := s5; writeln(c2);
  c3 := 'z'; writeln(c3)
end.
