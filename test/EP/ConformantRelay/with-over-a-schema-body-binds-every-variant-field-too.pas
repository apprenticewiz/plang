(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 22
*)

program p(output);
type box(n: integer) = record kind: integer;
       case tag: integer of 1: (one: integer; two: integer); 2: (r: real)
     end;
var b: box(4);
begin b.kind := 9; b.tag := 1;
  with b do begin one := 11; two := 22 end;
  writeln(b.one, ' ', b.two)
end.
