(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:date ok
CHECK-NEXT:year ok
CHECK-NEXT:time ok
*)

program p;
var t: TimeStamp;
begin
  GetTimeStamp(t);
  if t.DateValid then writeln('date ok') else writeln('date fail');
  if t.year >= 2024 then writeln('year ok') else writeln('year fail');
  if t.TimeValid then writeln('time ok') else writeln('time fail')
end.
