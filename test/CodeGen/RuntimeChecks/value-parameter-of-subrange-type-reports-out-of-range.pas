(*
RUN: %plang %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: value 999 out of range 1..10
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
    n := 999;
    show(n)
end.
