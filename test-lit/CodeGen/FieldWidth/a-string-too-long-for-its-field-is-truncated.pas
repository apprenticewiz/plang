(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:  hello, world
CHECK-NEXT: hello, world
CHECK-NEXT:hello, world
CHECK-NEXT:hello, worl
CHECK-NEXT:hello, wor
CHECK-NEXT:hello, wo
CHECK-NEXT:hello, w
CHECK-NEXT:hello, 
CHECK-NEXT:hello,
CHECK-NEXT:hello
CHECK-NEXT:hell
CHECK-NEXT:hel
CHECK-NEXT:he
CHECK-NEXT:h
*)

program p(output);
var i: integer;
begin for i := 14 downto 1 do writeln('hello, world': i) end.
