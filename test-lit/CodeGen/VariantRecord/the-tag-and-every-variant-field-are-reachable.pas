(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:c 0 5
CHECK-NEXT:1 2 3
*)

program p(output);
type shape = (circle, rect);
     fig = record
       name: char;
       case kind: shape of
         circle: (r: integer);
         rect:   (w, h: integer)
     end;
var f: fig;
begin
  f.name := 'c'; f.kind := circle; f.r := 5;
  writeln(f.name, ' ', ord(f.kind), ' ', f.r);
  f.kind := rect; f.w := 2; f.h := 3;
  writeln(ord(f.kind), ' ', f.w, ' ', f.h)
end.
