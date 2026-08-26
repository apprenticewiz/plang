(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:before
CHECK-NEXT:after captured
*)

program p;
var S: string(20);
begin
  writeln('before');
  writestr(S, 'captured');
  writeln('after ', S)
end.
