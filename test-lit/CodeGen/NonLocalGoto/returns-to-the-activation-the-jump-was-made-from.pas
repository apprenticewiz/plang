(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:fell through at 3
CHECK-NEXT:left 3 k=30
CHECK-NEXT:left 2 k=20
CHECK-NEXT:fell through at 1
CHECK-NEXT:left 1 k=10
*)

program p(output);
procedure search(depth: integer);
label 1;
var k: integer;
  procedure check(d: integer);
  begin if d = 2 then goto 1 end;
begin
  k := depth * 10;
  if depth < 3 then search(depth + 1);
  check(depth);
  writeln('fell through at ', depth:1);
1:
  writeln('left ', depth:1, ' k=', k:1)
end;
begin search(1) end.
