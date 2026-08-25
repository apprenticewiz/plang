(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true true true true
CHECK-NEXT:false false false false
*)

program p;
var t, f: boolean;
begin t := true; f := false;
 writeln(f < t, ' ', f <= t, ' ', t > f, ' ', t >= f);
 writeln(t < f, ' ', t <= f, ' ', f > t, ' ', f >= t) end.
