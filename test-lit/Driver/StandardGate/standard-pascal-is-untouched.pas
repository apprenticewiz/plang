(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:one abc 3 true
*)

program p;
var s: set of 1..5; i: integer; a: packed array[1..3] of char;
begin
  s := [1, 3]; i := 0;
  if 3 in s then i := succ(i);
  case i of 1: write('one'); 2: write('two') end;
  a := 'abc'; writeln(' ', a, ' ', round(2.6), ' ', odd(i))
end.
