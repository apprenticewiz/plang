(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-2 0 4 
CHECK-NEXT:-4 6 
CHECK-NEXT:-1 3 
CHECK-NEXT:-5 -1 3 
CHECK-NEXT:true
*)

program p;
type r = -5..10; s = set of r;
var g: s; i: r;
function pick(n: integer): s;
begin pick := [-4, 6] end;
procedure show(v: s);
  var k: r;
begin for k in v do write(k, ' '); writeln end;
begin
  show([-2, 0, 4]);
  show(pick(0));
  g := [-1] + [3];
  show(g);
  show(g + [-5]);
  i := -1;
  writeln(i in [-5 .. -1])
end.
