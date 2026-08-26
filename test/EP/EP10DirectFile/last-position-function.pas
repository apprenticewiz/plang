(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
*)

program p;
var f: file of integer;
    v, lp: integer;
begin
  rewrite(f);
  v := 10; write(f, v);
  v := 20; write(f, v);
  v := 30; write(f, v);
  lp := lastposition(f);
  writeln(lp)
end.
