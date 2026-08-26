(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3.0 2.5
*)

program p;
var f: file of real;
    r: real;
begin
  rewrite(f);
  write(f, 3);
  write(f, 2.5);
  reset(f);
  read(f, r); write(r:0:1, ' ');
  read(f, r); writeln(r:0:1)
end.
