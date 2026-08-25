(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true
CHECK-NEXT:false true
*)

program p(output);
type tb = false..true;
var x: tb; b: boolean;
begin x := true; b := true; writeln(x = b);
      b := false; writeln(x = b, ' ', x > b) end.
