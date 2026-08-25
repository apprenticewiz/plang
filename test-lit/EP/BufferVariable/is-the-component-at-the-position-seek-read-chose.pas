(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2 4 5
CHECK-NEXT:3 6
*)

program p;
var f: file[1..10] of integer;
    i: integer;
begin
  rewrite(f);
  for i := 1 to 5 do write(f, i * 2);
  seekread(f, 2);
  writeln(position(f), ' ', f^, ' ', lastposition(f));
  get(f);
  writeln(position(f), ' ', f^)
end.
