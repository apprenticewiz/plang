(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true
CHECK-NEXT:true false
*)

program p(output);
type day = (mon, tue, wed, thu, fri, sat, sun);
     weekday = mon..fri;
var c: weekday; d: day;
begin c := wed; d := wed; writeln(c = d);
      d := thu; writeln(c < d, ' ', c >= d) end.
