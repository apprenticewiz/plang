(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
CHECK-NEXT:4
*)

program p;
type
    digit = 1..10;
var d: digit;
procedure bump(var x: digit);
begin
    x := x + 1
end;
begin
    d := 3;
    writeln(d);
    bump(d);
    writeln(d)
end.
