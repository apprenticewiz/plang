(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

program p;
type
    solo = 5..5;
var n: integer;
procedure show(x: solo);
begin
    writeln(x)
end;
begin
    n := 5;
    show(n)
end.
