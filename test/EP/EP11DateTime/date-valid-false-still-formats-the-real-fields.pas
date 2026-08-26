(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2024-06-15
CHECK-NEXT:14:30:05
*)

program p;
var t: TimeStamp;
begin
  t.DateValid := false;
  t.year := 2024; t.month := 6; t.day := 15;
  t.TimeValid := false;
  t.hour := 14; t.minute := 30; t.second := 5;
  writeln(date(t)); writeln(time(t))
end.
