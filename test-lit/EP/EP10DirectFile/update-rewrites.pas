(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
CHECK-NEXT:99
*)

program p;
var f: file of integer;
    v: integer;
begin
  rewrite(f);
  v := 7; write(f, v);
  update(f);
  read(f, v);
  writeln(v);
  update(f);
  v := 99; write(f, v);
  update(f);
  read(f, v);
  writeln(v)
end.
