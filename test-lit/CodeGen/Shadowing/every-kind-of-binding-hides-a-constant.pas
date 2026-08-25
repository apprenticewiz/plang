(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5 3 123 99
*)

program p(output);
const size = 10; red = 7;
procedure byparam(size: integer); begin write(size, ' ') end;
procedure byenum; var red: integer; begin red := 3; write(red, ' ') end;
procedure byfor; var size: integer;
begin for size := 1 to 3 do write(size); write(' ') end;
procedure bywith;
type r = record size: integer end;
var rr: r;
begin rr.size := 99; with rr do write(size) end;
begin byparam(5); byenum; byfor; bywith; writeln end.
