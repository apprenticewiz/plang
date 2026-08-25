(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hello!
CHECK-NEXT:true
*)

program p(output);
var s: packed array[1..5] of char; v: string(20);
begin s := 'hello'; v := s;
  writeln(v, '!'); writeln(s = 'hello') end.
