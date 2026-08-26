(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

program p;
type
    digit = 1..10;
var n: integer;
procedure show(x: digit);
begin
    writeln(x)
end;
begin
    n := 7;
    show(n)
end.
