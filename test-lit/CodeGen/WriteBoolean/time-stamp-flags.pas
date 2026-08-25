(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:DateValid=true TimeValid=true
*)

program p;
var t: TimeStamp;
begin
  GetTimeStamp(t);
  writeln('DateValid=', t.DateValid, ' TimeValid=', t.TimeValid)
end.
