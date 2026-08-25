(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
CHECK-NEXT:8
*)

program p;
var t: TimeStamp;
begin
  GetTimeStamp(t);
  writeln(length(date(t)));
  writeln(length(time(t)))
end.
