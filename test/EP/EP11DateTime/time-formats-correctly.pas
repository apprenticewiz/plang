(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:14:30:05
*)

program p;
var t: TimeStamp;
begin
  t.DateValid := false;
  t.year := 1; t.month := 1; t.day := 1;
  t.TimeValid := true;
  t.hour := 14; t.minute := 30; t.second := 5;
  writeln(time(t))
end.
