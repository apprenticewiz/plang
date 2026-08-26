(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:20
CHECK-NEXT:10
CHECK-NEXT:30
*)

program p;
var f: file of integer;
    v: integer;
begin
  rewrite(f);
  v := 10; seekwrite(f, 0); write(f, v);
  v := 20; seekwrite(f, 1); write(f, v);
  v := 30; seekwrite(f, 2); write(f, v);
  seekread(f, 1);
  read(f, v);
  writeln(v);
  seekread(f, 0);
  read(f, v);
  writeln(v);
  seekread(f, 2);
  read(f, v);
  writeln(v)
end.
