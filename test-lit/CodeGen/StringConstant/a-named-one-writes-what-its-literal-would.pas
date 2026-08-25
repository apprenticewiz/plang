(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:this is a string
CHECK-NEXT:this is a string
*)

program p(output);
const s = 'this is a string';
begin writeln(s); writeln('this is a string') end.
