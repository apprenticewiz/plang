(*
RUN: %plang -std=iso10206 -O2 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:date ok
CHECK-NEXT:time ok
*)

program p;
var t: TimeStamp;
begin
  GetTimeStamp(t);
  if t.DateValid then writeln('date ok') else writeln('date bad');
  if t.TimeValid then writeln('time ok') else writeln('time bad')
end.
