(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2025 6 15
*)

program p;
var t: TimeStamp;
begin
  t.DateValid := true;
  t.year := 2025; t.month := 6; t.day := 15;
  t.TimeValid := false;
  t.hour := 0; t.minute := 0; t.second := 0;
  writeln(t.year, ' ', t.month, ' ', t.day)
end.
