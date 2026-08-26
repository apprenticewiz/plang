(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
CHECK-NEXT:0
CHECK-NEXT:2
*)

program p;
var f: file of integer;
    v, p: integer;
begin
  rewrite(f);
  v := 1; write(f, v);
  v := 2; write(f, v);
  v := 3; write(f, v);
  p := position(f);
  writeln(p);
  seekwrite(f, 0);
  p := position(f);
  writeln(p);
  seekwrite(f, 2);
  p := position(f);
  writeln(p)
end.
