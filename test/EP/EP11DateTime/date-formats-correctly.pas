(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2026-08-08
*)

program p;
var t: TimeStamp;
begin
  t.DateValid := true;
  t.year := 2026; t.month := 8; t.day := 8;
  t.TimeValid := false;
  t.hour := 0; t.minute := 0; t.second := 0;
  writeln(date(t))
end.
