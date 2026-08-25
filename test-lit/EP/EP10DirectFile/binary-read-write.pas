(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
CHECK-NEXT:99
*)

program p;
var f: file of integer;
    v: integer;
begin
  rewrite(f);
  v := 42;
  write(f, v);
  v := 99;
  write(f, v);
  update(f);
  read(f, v);
  writeln(v);
  read(f, v);
  writeln(v)
end.
