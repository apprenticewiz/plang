(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:205
*)

program p;
var f: file of integer;
    v: integer;
begin
  rewrite(f);
  v := 100; seekwrite(f, 0); write(f, v);
  v := 200; seekwrite(f, 1); write(f, v);
  seekupdate(f, 1);
  read(f, v);
  v := v + 5;
  seekwrite(f, 1);
  write(f, v);
  seekread(f, 1);
  read(f, v);
  writeln(v)
end.
