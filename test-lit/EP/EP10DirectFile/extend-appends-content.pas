(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11
CHECK-NEXT:22
CHECK-NEXT:33
*)

program p;
var f: file of integer;
    v: integer;
begin
  rewrite(f);
  v := 11; write(f, v);
  v := 22; write(f, v);
  extend(f);
  v := 33; write(f, v);
  update(f);
  read(f, v); writeln(v);
  read(f, v); writeln(v);
  read(f, v); writeln(v)
end.
