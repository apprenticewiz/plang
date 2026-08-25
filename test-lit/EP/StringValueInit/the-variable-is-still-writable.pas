(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hi
CHECK-NEXT:there
*)

program p;
var s: string(10) value 'hi';
begin writeln(s); s := 'there'; writeln(s) end.
